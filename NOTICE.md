# Notices

MochaScript is MIT licensed. Its initial language implementation is derived from Godot Engine's
GDScript module, whose source is MIT licensed and copyrighted by the Godot Engine contributors and
Juan Linietsky, Ariel Manzur. Imported files retain their upstream notices.

The GDExtension scaffold is derived from
[`godotengine/godot-cpp-template`](https://github.com/godotengine/godot-cpp-template) at commit
`d36f9d03e9e02dad22829f1faf4e03a347616df5`.

The initial language/resource integration follows patterns proven in Cafecito Games'
BaristaScript, especially its `ScriptLanguageExtension`, `ScriptExtension`, resource-loader, and
registration lifecycle. BaristaScript is also MIT licensed by Cafecito Games LLC. MochaScript does
not use BaristaScript's language frontend as its semantic source.

Exact source revisions and generated API identity are recorded in `upstream.lock.json`.
