/**************************************************************************/
/*  mocha_platform.h                                                      */
/**************************************************************************/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*                                                                        */
/*  Platform-seam structure adapted from BaristaScript's bs_platform.h.   */
/*  See NOTICE.md.                                                        */
/*                                                                        */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

// This is the only compatibility seam used by imported language-core code.
// An upstream core dependency is mapped here only when godot-cpp exposes an
// equivalent public API. Missing behavior must be adapted explicitly rather
// than hidden behind a no-op shim.

#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/text_server_manager.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/char_utils.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

// Imported Godot-module code uses these names in the global namespace. Keeping
// the mapping here makes the mechanical baseline-to-Mocha delta auditable.
using namespace godot;

#if !defined(ERR_FAIL_COND) || !defined(ERR_FAIL_COND_MSG) || !defined(ERR_FAIL_COND_V)
#error "godot-cpp no longer provides the error macros required by MochaScript's imported core"
#endif
#if !defined(ERR_FAIL_INDEX_V_MSG)
#error "godot-cpp no longer provides the index-check macro required by MochaScript's imported core"
#endif
