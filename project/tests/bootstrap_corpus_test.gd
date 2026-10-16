extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	for path in [
		"res://tests/upstream/parser/array.mocha",
		"res://tests/upstream/parser/while.mocha",
	]:
		var script := ResourceLoader.load(path) as MochaScript
		if script == null or script.reload() != OK or not script.is_compilation_valid():
			failures.append("pinned bootstrap fixture failed: %s: %s" % [path, script.get_diagnostics() if script else []])

	var negative_path := "res://tests/upstream/analyzer/incompatible_assignment.mocha"
	var negative := ResourceLoader.load(negative_path) as MochaScript
	if negative == null or negative.reload() != ERR_COMPILATION_FAILED:
		failures.append("pinned negative fixture did not fail analysis")
	elif negative.get_diagnostics().is_empty() or negative.get_diagnostics()[0].get("code") != "M2003":
		failures.append("pinned negative fixture did not produce M2003")

	if failures.is_empty():
		print("MOCHA_BOOTSTRAP_CORPUS_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)
