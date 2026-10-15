extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	if not Engine.is_editor_hint():
		failures.append("editor behavior test did not run with the editor hint")
	var example_uid := ResourceLoader.get_resource_uid("res://example.mocha")
	if example_uid < 0:
		failures.append("example.mocha has no editor-generated resource UID")
	elif not ResourceUID.has_id(example_uid) or ResourceUID.get_id_path(example_uid) != "res://example.mocha":
		failures.append("example.mocha UID is not registered to its resource path")
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
		if example_uid >= 0 and ResourceUID.has_id(example_uid) and ResourceLoader.load(ResourceUID.id_to_text(example_uid)) != script:
			failures.append("example.mocha did not resolve through its registered UID")

	var save_path := "user://mocha_editor_save_test.mocha"
	var unsaved := MochaScript.new()
	unsaved.set_source_code("extends RefCounted\nfunc saved_value() -> int:\n\treturn 7\n")
	if ResourceSaver.save(unsaved, save_path) != OK:
		failures.append("editor could not save a new MochaScript")
	else:
		var saved := ResourceLoader.load(save_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
		if saved == null or not saved.is_compilation_valid() or not saved.get_source_code().contains("return 7"):
			failures.append("editor-saved MochaScript did not reload with its source")

	if failures.is_empty():
		print("MOCHA_EDITOR_OK")
		return
	for failure in failures:
		push_error(failure)
	quit(1)
