extends SceneTree


func _initialize() -> void:
	var failures := MochaNativeTests.new().run()
	if failures.is_empty():
		print("MOCHA_NATIVE_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)
