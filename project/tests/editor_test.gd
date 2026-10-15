extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	if not Engine.is_editor_hint():
		failures.append("editor behavior test did not run with the editor hint")
	var script := ResourceLoader.load("res://example.mocha") as MochaScript
	if script == null or not script.is_compilation_valid():
		failures.append("example.mocha was not a valid MochaScript in editor context")
	elif script.can_instantiate():
		failures.append("non-tool MochaScript can instantiate in editor context")
	else:
		var host := Node.new()
		host.set_script(script)
		if host.get_script() != script:
			failures.append("editor did not attach the MochaScript placeholder")
		elif not script.is_placeholder_instance(host):
			failures.append("editor attachment has no MochaScript placeholder instance")
		if host.get(&"total") == 1:
			failures.append("editor attachment ran the MochaScript member initializer")
		host.free()

	if failures.is_empty():
		print("MOCHA_EDITOR_OK")
		return
	for failure in failures:
		push_error(failure)
	quit(1)
