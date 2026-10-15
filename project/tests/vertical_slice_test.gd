extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	var script := ResourceLoader.load("res://example.mocha") as MochaScript
	if script == null:
		failures.append("example.mocha did not load")
	else:
		var compute_metadata: Dictionary = {}
		for method in script.get_script_method_list():
			if method.get("name") == &"compute":
				compute_metadata = method
				break
		if compute_metadata.is_empty() or compute_metadata.get("args", []).size() != 1:
			failures.append("script method metadata omitted compute's parameter")
		elif compute_metadata.get("return", {}).get("type") != TYPE_INT:
			failures.append("script method metadata omitted compute's int return")
		var host := Node.new()
		host.name = "MochaHost"
		host.set_script(script)
		var published_source := script.get_source_code()
		script.set_source_code("extends Node\n")
		if script.get_source_code() != published_source:
			failures.append("source changed underneath a live MochaScript instance")
		elif script.get_last_source_error().get("code") != "M0003" or not script.get_diagnostics().is_empty():
			failures.append("live source replacement did not produce M0003")
		if script.reload() != ERR_ALREADY_IN_USE:
			failures.append("reload was not refused while a live MochaScript instance existed")
		if host.get_script() != script:
			failures.append("MochaScript did not attach to the Godot Node")
		elif not host.has_method("compute"):
			failures.append("compiled compute method is not visible through Godot")
		else:
			if not str(host).contains("MochaHost"):
				failures.append("script instance replaced the host's native string representation")
			var instance_compute_metadata: Dictionary = {}
			for method in host.get_method_list():
				if method.get("name") == &"compute":
					instance_compute_metadata = method
					break
			if instance_compute_metadata.is_empty() or instance_compute_metadata.get("args", []).size() != 1:
				failures.append("instance method metadata omitted compute's parameter")
			elif instance_compute_metadata.get("args", [])[0].get("name") != &"input" or instance_compute_metadata.get("args", [])[0].get("type") != TYPE_INT:
				failures.append("instance method metadata lost compute's typed parameter: %s" % instance_compute_metadata)
			elif instance_compute_metadata.get("return", {}).get("type") != TYPE_INT:
				failures.append("instance method metadata omitted compute's int return")
			var total_metadata: Dictionary = {}
			for property in host.get_property_list():
				if property.get("name") == &"total":
					total_metadata = property
					break
			if total_metadata.is_empty() or total_metadata.get("type") != TYPE_INT:
				failures.append("instance property metadata omitted total's int type")
			var result: Variant = host.call("compute", 1)
			if result != 42:
				failures.append("compute returned %s instead of 42" % result)
			if host.get("total") != 42:
				failures.append("compiled member store did not persist total = 42")
			if host.call("language_version") != "MochaScript/bootstrap-1":
				failures.append("Mocha-owned language evolution canary did not execute")
			var detach_result: Variant = host.call("detach_self")
			if detach_result != 1 or host.get_script() != null:
				failures.append("script instance did not survive and complete a self-detach call safely")
		host.free()

	var transient := MochaScript.new()
	transient.set_source_code("extends RefCounted\nfunc answer() -> int:\n\treturn 42\n")
	if transient.reload() != OK or not transient.can_instantiate():
		failures.append("transient source did not compile")
	else:
		transient.set_source_code("extends RefCounted\nfunc answer() -> int:\n\treturn 43\n")
		if transient.is_compilation_valid() or transient.can_instantiate():
			failures.append("changed source retained a stale compiled module")
		elif transient.reload() != OK or not transient.can_instantiate():
			failures.append("changed transient source did not recompile")

	if failures.is_empty():
		print("MOCHA_EXECUTION_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)
