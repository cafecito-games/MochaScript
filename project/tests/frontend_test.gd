extends SceneTree


func _check_failure(path: String, expected_error: Error, expected_code: String, expected_line: int, expected_column: int, failures: Array[String]) -> void:
	var script := ResourceLoader.load(path) as MochaScript
	if script == null:
		failures.append("%s did not load as MochaScript" % path)
		return
	var status := script.reload()
	if status != expected_error:
		failures.append("%s returned %s instead of %s" % [path, status, expected_error])
	var diagnostics := script.get_diagnostics()
	if diagnostics.is_empty():
		failures.append("%s produced no diagnostic" % path)
		return
	var diagnostic: Dictionary = diagnostics[0]
	if diagnostic.get("code") != expected_code:
		failures.append("%s produced %s instead of %s" % [path, diagnostic.get("code"), expected_code])
	if diagnostic.get("path") != path:
		failures.append("%s diagnostic reported path %s" % [path, diagnostic.get("path")])
	if diagnostic.get("line") != expected_line or diagnostic.get("column") != expected_column:
		failures.append("%s diagnostic reported %s:%s instead of %s:%s" % [path, diagnostic.get("line"), diagnostic.get("column"), expected_line, expected_column])


func _initialize() -> void:
	var failures: Array[String] = []
	var positive := ResourceLoader.load("res://example.mocha") as MochaScript
	if positive == null or positive.reload() != OK or not positive.is_compilation_valid():
		failures.append("the parser/analyzer rejected example.mocha: %s" % (positive.get_diagnostics() if positive else []))

	_check_failure("res://tests/negative_syntax.mocha", ERR_PARSE_ERROR, "M1001", 3, 24, failures)
	_check_failure("res://tests/negative_type.mocha", ERR_COMPILATION_FAILED, "M2022", 3, 1, failures)

	if failures.is_empty():
		print("MOCHA_FRONTEND_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)
