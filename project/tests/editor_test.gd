extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	if not Engine.is_editor_hint():
		failures.append("editor behavior test did not run with the editor hint")
	var example_uid := ResourceLoader.get_resource_uid("res://example.mocha")
	if example_uid < 0:
		failures.append("example.mocha has no resource UID")
	elif not ResourceUID.has_id(example_uid) or ResourceUID.get_id_path(example_uid) != "res://example.mocha":
		failures.append("example.mocha UID is not registered to its resource path")
	else:
		var uid_file := FileAccess.open("res://example.mocha.uid", FileAccess.READ)
		if uid_file == null:
			failures.append("example.mocha committed UID sidecar could not be read")
		elif ResourceUID.text_to_id(uid_file.get_line().strip_edges()) != example_uid:
			failures.append("example.mocha registered UID does not match its committed sidecar")
		if uid_file != null:
			uid_file.close()
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
	if ResourceSaver.save(unsaved, "res://example.mocha/cannot_write.mocha") == OK:
		failures.append("editor reported success while saving MochaScript to an unwritable path")
	if ResourceSaver.save(unsaved, save_path) != OK:
		failures.append("editor could not save a new MochaScript")
	else:
		var saved := ResourceLoader.load(save_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
		if saved == null or not saved.is_compilation_valid() or not saved.get_source_code().contains("return 7"):
			failures.append("editor-saved MochaScript did not reload with its source")
		unsaved.set_source_code("extends RefCounted\nfunc saved_value() -> int:\n\treturn 8\n")
		if ResourceSaver.save(unsaved, save_path) != OK:
			failures.append("editor could not atomically replace a saved MochaScript")
		else:
			saved = ResourceLoader.load(save_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
			if saved == null or not saved.get_source_code().contains("return 8"):
				failures.append("replaced MochaScript did not contain the latest saved source")

	var bom_script := MochaScript.new()
	var bom_source := "\ufeffextends RefCounted\n"
	bom_script.set_source_code(bom_source)
	var bom_path := "user://mocha_editor_bom_save_test.mocha"
	if ResourceSaver.save(bom_script, bom_path) != OK or FileAccess.get_file_as_bytes(bom_path) != bom_source.to_utf8_buffer():
		failures.append("editor saver did not preserve leading-BOM source bytes")

	var target_path := "user://mocha_editor_symlink_target.mocha"
	var middle_path := "user://mocha_editor_symlink_middle.mocha"
	var link_path := "user://mocha_editor_symlink.mocha"
	var target_file := FileAccess.open(target_path, FileAccess.WRITE)
	if target_file == null:
		failures.append("editor could not create the symlink save target")
	else:
		target_file.store_string("extends RefCounted\n")
		target_file.close()
		var user_dir := DirAccess.open("user://")
		var target_absolute := ProjectSettings.globalize_path(target_path)
		var middle_absolute := ProjectSettings.globalize_path(middle_path)
		var link_absolute := ProjectSettings.globalize_path(link_path)
		var permission_preserved := FileAccess.set_unix_permissions(target_path, 416) == OK
		if user_dir != null and user_dir.create_link(target_absolute, middle_absolute) == OK and user_dir.create_link(middle_absolute, link_absolute) == OK:
			unsaved.set_source_code("extends RefCounted\nfunc saved_value() -> int:\n\treturn 9\n")
			if ResourceSaver.save(unsaved, link_path) != OK:
				failures.append("editor could not save a MochaScript through a symlink chain")
			elif not user_dir.is_link(link_absolute) or not user_dir.is_link(middle_absolute) or not FileAccess.get_file_as_string(target_path).contains("return 9"):
				failures.append("saving through a symlink chain replaced a link instead of its target")
			elif permission_preserved and FileAccess.get_unix_permissions(target_path) != 416:
				failures.append("saving through a symlink changed the target permissions")

	if failures.is_empty():
		print("MOCHA_EDITOR_OK")
		return
	for failure in failures:
		push_error(failure)
	quit(1)
