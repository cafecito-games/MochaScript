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
		var other_script := ResourceLoader.load("res://example.mocha", "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
		if other_script == null or other_script.instance_has(host):
			failures.append("a different MochaScript treated the editor placeholder as its runtime instance")
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
	else:
		var loaded_bom := ResourceLoader.load(bom_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
		if loaded_bom == null or not loaded_bom.is_compilation_valid() or loaded_bom.get_source_code() != bom_source or ResourceSaver.save(loaded_bom, bom_path) != OK or FileAccess.get_file_as_bytes(bom_path) != bom_source.to_utf8_buffer():
			failures.append("leading-BOM source did not survive a load/save round trip")
	var double_bom_source := "\ufeff" + bom_source
	var double_bom_path := "user://mocha_editor_double_bom_save_test.mocha"
	bom_script.set_source_code(double_bom_source)
	if ResourceSaver.save(bom_script, double_bom_path) != OK:
		failures.append("editor saver could not persist double-BOM source")
	else:
		var loaded_double_bom := ResourceLoader.load(double_bom_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
		if loaded_double_bom == null or loaded_double_bom.get_source_code() != double_bom_source or ResourceSaver.save(loaded_double_bom, double_bom_path) != OK or FileAccess.get_file_as_bytes(double_bom_path) != double_bom_source.to_utf8_buffer():
			failures.append("double-BOM source did not survive a load/save round trip")

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
		else:
			failures.append("editor could not establish the symlink-chain save fixture")

	var real_parent := "user://mocha_editor_real_parent"
	var alias_parent := "user://mocha_editor_alias_parent"
	if DirAccess.make_dir_absolute(ProjectSettings.globalize_path(real_parent)) != OK:
		failures.append("editor could not create the relative-link save fixture directory")
	else:
		var real_dir := DirAccess.open(real_parent)
		var fixture_root := DirAccess.open("user://")
		var relative_target := real_parent.path_join("target.mocha")
		var relative_target_file := FileAccess.open(relative_target, FileAccess.WRITE)
		if real_dir == null or fixture_root == null or relative_target_file == null:
			failures.append("editor could not prepare the relative-link save fixture")
		else:
			relative_target_file.store_string("extends RefCounted\n")
			relative_target_file.close()
			var relative_link_absolute := ProjectSettings.globalize_path(real_parent.path_join("link.mocha"))
			var alias_absolute := ProjectSettings.globalize_path(alias_parent)
			if real_dir.create_link("target.mocha", relative_link_absolute) != OK or fixture_root.create_link(ProjectSettings.globalize_path(real_parent), alias_absolute) != OK:
				failures.append("editor could not establish the relative-link save fixture")
			elif ResourceSaver.save(unsaved, alias_parent.path_join("link.mocha")) != OK:
				failures.append("editor saver could not follow a safe relative link beneath a symlinked parent")
			elif not FileAccess.get_file_as_string(relative_target).contains("return 9"):
				failures.append("safe relative-link save did not modify the real target")

	var traversal_root := "user://mocha_editor_traversal"
	var traversal_elsewhere := "user://mocha_editor_traversal_elsewhere"
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(traversal_root)) != OK or DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(traversal_elsewhere.path_join("sub"))) != OK:
		failures.append("editor could not create the parent-traversal save fixture directories")
	else:
		var traversal_dir := DirAccess.open(traversal_root)
		var lexical_target := traversal_root.path_join("target.mocha")
		var real_target := traversal_elsewhere.path_join("target.mocha")
		var lexical_file := FileAccess.open(lexical_target, FileAccess.WRITE)
		var real_file := FileAccess.open(real_target, FileAccess.WRITE)
		if traversal_dir == null or lexical_file == null or real_file == null:
			failures.append("editor could not prepare the parent-traversal save fixture")
		else:
			lexical_file.store_string("extends RefCounted\n")
			lexical_file.close()
			real_file.store_string("extends RefCounted\n")
			real_file.close()
			var alias_path := ProjectSettings.globalize_path(traversal_root.path_join("alias"))
			var link_path_with_parent := ProjectSettings.globalize_path(traversal_root.path_join("link.mocha"))
			if traversal_dir.create_link(ProjectSettings.globalize_path(traversal_elsewhere.path_join("sub")), alias_path) != OK or OS.execute("ln", ["-s", "alias/../target.mocha", link_path_with_parent]) != 0:
				failures.append("editor could not establish the parent-traversal save fixture")
			elif ResourceSaver.save(unsaved, traversal_root.path_join("link.mocha")) != ERR_CANT_RESOLVE:
				failures.append("editor saver did not reject a symlink target containing parent traversal")
			elif FileAccess.get_file_as_string(lexical_target).contains("return 9") or FileAccess.get_file_as_string(real_target).contains("return 9"):
				failures.append("rejected parent-traversal save modified a possible target")

	if failures.is_empty():
		print("MOCHA_EDITOR_OK")
		return
	for failure in failures:
		push_error(failure)
	quit(1)
