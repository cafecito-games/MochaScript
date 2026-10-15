extends SceneTree


func _write_script(path: String, value: int) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string("extends RefCounted\nfunc value() -> int:\n\treturn %d\n" % value)
	file.close()
	return true


func _initialize() -> void:
	var failures: Array[String] = []
	var release_smoke := OS.get_cmdline_user_args().has("--release-smoke")
	var native_tests_registered := ClassDB.class_exists(&"MochaNativeTests")
	if release_smoke and native_tests_registered:
		failures.append("release library exposed the debug-only MochaNativeTests class")
	elif not release_smoke and not native_tests_registered:
		failures.append("debug library did not register MochaNativeTests")
	var recognized := ResourceLoader.get_recognized_extensions_for_type("MochaScript")
	if not recognized.has("mocha"):
		failures.append("MochaScript does not recognize .mocha")

	var script := ResourceLoader.load("res://example.mocha") as Script
	if script == null:
		failures.append("example.mocha did not load as Script")
	else:
		if script.get_class() != "MochaScript":
			failures.append("loaded resource is %s" % script.get_class())
		if not script.get_source_code().begins_with("# MochaScript executable vertical slice\n"):
			failures.append("source text was not preserved")
		if not script.is_compilation_valid():
			failures.append("tokenizer rejected the valid recognition fixture: %s" % script.get_diagnostics())
		if script.get_token_count() < 20:
			failures.append("recognition fixture did not traverse the Mocha tokenizer")
		if not script.can_instantiate():
			failures.append("compiled MochaScript unexpectedly cannot instantiate")

	var cache_path := "user://mocha_cache_replace_test.mocha"
	if not _write_script(cache_path, 1):
		failures.append("could not write the cache-mode fixture")
	else:
		var cached := ResourceLoader.load(cache_path) as MochaScript
		if cached == null or not cached.is_compilation_valid():
			failures.append("initial cache-mode fixture did not compile")
		elif not _write_script(cache_path, 2):
			failures.append("could not replace the cache-mode fixture")
		else:
			var ignored := ResourceLoader.load(cache_path, "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
			if ignored == null or ignored == cached or not ignored.get_source_code().contains("return 2") or not ignored.is_compilation_valid():
				failures.append("CACHE_MODE_IGNORE did not return a valid independent updated script")
			var replaced := ResourceLoader.load(cache_path, "", ResourceLoader.CACHE_MODE_REPLACE) as MochaScript
			if replaced != cached or not replaced.get_source_code().contains("return 2") or not replaced.is_compilation_valid():
				failures.append("CACHE_MODE_REPLACE did not update and recompile the cached script: same=%s source=%s valid=%s diagnostics=%s" % [replaced == cached, replaced.get_source_code() if replaced else "<null>", replaced.is_compilation_valid() if replaced else false, replaced.get_diagnostics() if replaced else []])

	if failures.is_empty():
		print("MOCHA_RECOGNITION_OK")
		quit(0)
		return

	for failure in failures:
		push_error(failure)
	quit(1)
