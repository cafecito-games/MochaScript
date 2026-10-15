extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	var script := ResourceLoader.load("res://example.mocha") as MochaScript
	if script == null:
		failures.append("example.mocha did not load")
	else:
		var host := Node.new()
		host.name = "MochaHost"
		host.set_script(script)
		if host.get_script() != script:
			failures.append("MochaScript did not attach to the Godot Node")
		elif not host.has_method("compute"):
			failures.append("compiled compute method is not visible through Godot")
		else:
			var result: Variant = host.call("compute", 1)
			if result != 42:
				failures.append("compute returned %s instead of 42" % result)
			if host.get("total") != 42:
				failures.append("compiled member store did not persist total = 42")
			if host.call("language_version") != "MochaScript/bootstrap-1":
				failures.append("Mocha-owned language evolution canary did not execute")
		host.free()

	if failures.is_empty():
		print("MOCHA_EXECUTION_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)
