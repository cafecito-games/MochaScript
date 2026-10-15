extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
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

	var ignored := ResourceLoader.load("res://example.mocha", "", ResourceLoader.CACHE_MODE_IGNORE) as MochaScript
	if ignored == null or not ignored.is_compilation_valid():
		failures.append("CACHE_MODE_IGNORE did not produce a valid independent MochaScript")
	var replaced := ResourceLoader.load("res://example.mocha", "", ResourceLoader.CACHE_MODE_REPLACE) as MochaScript
	if replaced == null or not replaced.is_compilation_valid():
		failures.append("CACHE_MODE_REPLACE did not produce a valid MochaScript")

	if failures.is_empty():
		print("MOCHA_RECOGNITION_OK")
		quit(0)
		return

	for failure in failures:
		push_error(failure)
	quit(1)
