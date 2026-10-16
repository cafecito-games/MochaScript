extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	var valid_source := "var answer: int = 40 + 2\n"
	var valid_script := MochaScript.new()
	valid_script.set_source_code(valid_source)
	var valid_error := valid_script.reload()
	if valid_error != OK:
		failures.append("valid source returned %s: %s" % [valid_error, valid_script.get_diagnostics()])
	if not valid_script.is_compilation_valid():
		failures.append("valid source was marked invalid")
	if valid_script.get_token_count() < 8:
		failures.append("valid source produced too few tokens")

	var invalid_script := MochaScript.new()
	invalid_script.set_source_code("var broken = \"unterminated\n")
	var invalid_error := invalid_script.reload()
	if invalid_error != ERR_PARSE_ERROR:
		failures.append("invalid source returned %s instead of ERR_PARSE_ERROR" % invalid_error)
	var diagnostics := invalid_script.get_diagnostics()
	if diagnostics.size() != 1:
		failures.append("invalid source produced %d diagnostics instead of one" % diagnostics.size())
	else:
		var diagnostic: Dictionary = diagnostics[0]
		if diagnostic.get("code") != "M0001":
			failures.append("unexpected diagnostic code: %s" % diagnostic.get("code"))
		if diagnostic.get("path") != "<memory>":
			failures.append("unexpected diagnostic path: %s" % diagnostic.get("path"))
		if diagnostic.get("line") != 1 or diagnostic.get("column") != 14:
			failures.append("unexpected diagnostic position: %s:%s" % [diagnostic.get("line"), diagnostic.get("column")])
		if diagnostic.get("message") != "Unterminated string.":
			failures.append("unexpected diagnostic message: %s" % diagnostic.get("message"))

	if failures.is_empty():
		print("MOCHA_TOKENIZER_OK")
		quit(0)
		return

	for failure in failures:
		push_error(failure)
	quit(1)
