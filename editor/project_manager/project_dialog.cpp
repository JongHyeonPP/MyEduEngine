/**************************************************************************/
/*  project_dialog.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "project_dialog.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/zip_io.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_icons.h"
#include "editor/themes/editor_scale.h"
#include "editor/version_control/editor_vcs_interface.h"
#include "scene/gui/center_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"

// DORO: Mascot image data
#include "doro_mascot_data.gen.h"

// DORO: Update card styles based on selection
void ProjectDialog::_update_doro_card_styles() {
	for (int i = 0; i < 4; i++) {
		if (doro_mode_cards[i] == nullptr) {
			continue;
		}
		Ref<StyleBoxFlat> card_style;
		card_style.instantiate();
		card_style->set_bg_color(Color(1.0, 1.0, 1.0));
		card_style->set_corner_radius_all(12);
		card_style->set_content_margin_all(12);

		if (i == (int)selected_doro_mode) {
			// Selected: blue border
			card_style->set_border_width_all(3);
			card_style->set_border_color(Color(0.20, 0.45, 0.80)); // Blue
		} else {
			// Not selected: gray border
			card_style->set_border_width_all(2);
			card_style->set_border_color(Color(0.88, 0.90, 0.93));
		}
		doro_mode_cards[i]->add_theme_style_override("panel", card_style);
	}
}

// DORO: Handle mode card click (receives InputEvent, but we bind the index)
void ProjectDialog::_doro_mode_selected(int p_mode) {
	selected_doro_mode = (DoroMode)p_mode;
	_update_doro_card_styles();
}

// DORO: Hide placeholder when input is clicked (gui_input approach for Web compatibility)
void ProjectDialog::_doro_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		if (doro_project_name && !doro_input_placeholder.is_empty()) {
			doro_project_name->set_placeholder(U"");
		}
	}
}

// DORO: Close modal when overlay is clicked - same as X button
void ProjectDialog::_doro_overlay_clicked() {
	hide(); // This is exactly what X button does
}

// DORO: Start button pressed
void ProjectDialog::_doro_start_pressed() {
	String name = doro_project_name->get_text().strip_edges();
	if (name.is_empty()) {
		name = U"새 프로젝트";
	}

	// Set project name for the actual creation
	project_name->set_text(name);

	// Use gl_compatibility renderer for DORO (web compatible)
	List<BaseButton *> buttons;
	renderer_button_group->get_buttons(&buttons);
	for (BaseButton *base_btn : buttons) {
		Button *btn = Object::cast_to<Button>(base_btn);
		if (btn && btn->get_meta(SNAME("rendering_method")) == "gl_compatibility") {
			btn->set_pressed(true);
			_renderer_selected();
			break;
		}
	}

	// Trigger OK
	ok_pressed();
}

void ProjectDialog::_set_message(const String &p_msg, MessageType p_type, InputType p_input_type) {
	msg->set_text(p_msg);
	get_ok_button()->set_disabled(p_type == MESSAGE_ERROR);

	Ref<Texture2D> new_icon;
	switch (p_type) {
		case MESSAGE_ERROR: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusError"));
		} break;
		case MESSAGE_WARNING: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusWarning"));
		} break;
		case MESSAGE_SUCCESS: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusSuccess"));
		} break;
	}

	if (p_input_type == PROJECT_PATH) {
		project_status_rect->set_texture(new_icon);
	} else if (p_input_type == INSTALL_PATH) {
		install_status_rect->set_texture(new_icon);
	}
}

static bool is_zip_file(Ref<DirAccess> p_d, const String &p_path) {
	return p_path.get_extension() == "zip" && p_d->file_exists(p_path);
}

void ProjectDialog::_validate_path() {
	_set_message("", MESSAGE_SUCCESS, PROJECT_PATH);
	_set_message("", MESSAGE_SUCCESS, INSTALL_PATH);

	if (project_name->get_text().strip_edges().is_empty()) {
		_set_message(TTRC("It would be a good idea to name your project."), MESSAGE_ERROR);
		return;
	}

	Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	String path = project_path->get_text().simplify_path();

	String target_path = path;
	InputType target_path_input_type = PROJECT_PATH;

	if (mode == MODE_IMPORT) {
		if (path.get_file().strip_edges() == "project.godot") {
			path = path.get_base_dir();
			project_path->set_text(path);
		}

		if (is_zip_file(d, path)) {
			zip_path = path;
		} else if (is_zip_file(d, path.strip_edges())) {
			zip_path = path.strip_edges();
		} else {
			zip_path = "";
		}

		if (!zip_path.is_empty()) {
			target_path = install_path->get_text().simplify_path();
			target_path_input_type = INSTALL_PATH;

			create_dir->show();
			install_path_container->show();

			Ref<FileAccess> io_fa;
			zlib_filefunc_def io = zipio_create_io(&io_fa);

			unzFile pkg = unzOpen2(zip_path.utf8().get_data(), &io);
			if (!pkg) {
				_set_message(TTRC("Invalid \".zip\" project file; it is not in ZIP format."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			int ret = unzGoToFirstFile(pkg);
			while (ret == UNZ_OK) {
				unz_file_info info;
				char fname[16384];
				ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				if (name.get_file() == "project.godot") {
					break; // ret == UNZ_OK.
				}

				ret = unzGoToNextFile(pkg);
			}

			if (ret == UNZ_END_OF_LIST_OF_FILE) {
				_set_message(TTRC("Invalid \".zip\" project file; it doesn't contain a \"project.godot\" file."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			unzClose(pkg);
		} else if (d->dir_exists(path) && d->file_exists(path.path_join("project.godot"))) {
			zip_path = "";

			create_dir->hide();
			install_path_container->hide();

			_set_message(TTRC("Valid project found at path."), MESSAGE_SUCCESS);
		} else {
			create_dir->hide();
			install_path_container->hide();

			_set_message(TTRC("Please choose a \"project.godot\", a directory with one, or a \".zip\" file."), MESSAGE_ERROR);
			return;
		}
	}

	if (target_path.is_relative_path()) {
		_set_message(TTRC("The path specified is invalid."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	if (target_path.get_file() != OS::get_singleton()->get_safe_dir_name(target_path.get_file())) {
		_set_message(TTRC("The directory name specified contains invalid characters or trailing whitespace."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	String working_dir = d->get_current_dir();
	String executable_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	if (target_path == working_dir || target_path == executable_dir) {
		_set_message(TTRC("Creating a project at the engine's working directory or executable directory is not allowed, as it would prevent the project manager from starting."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	// TODO: The following 5 lines could be simplified if OS.get_user_home_dir() or SYSTEM_DIR_HOME is implemented. See: https://github.com/godotengine/godot-proposals/issues/4851.
#ifdef WINDOWS_ENABLED
	String home_dir = OS::get_singleton()->get_environment("USERPROFILE");
#else
	String home_dir = OS::get_singleton()->get_environment("HOME");
#endif
	String documents_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (target_path == home_dir || target_path == documents_dir) {
		_set_message(TTRC("You cannot save a project at the selected path. Please create a subfolder or choose a new path."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	is_folder_empty = true;
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE || (mode == MODE_IMPORT && target_path_input_type == InputType::INSTALL_PATH)) {
		if (create_dir->is_pressed()) {
			if (!d->dir_exists(target_path.get_base_dir())) {
				_set_message(TTRC("The parent directory of the path specified doesn't exist."), MESSAGE_ERROR, target_path_input_type);
				return;
			}

			if (d->dir_exists(target_path)) {
				// The path is not necessarily empty here, but we will update the message later if it isn't.
				_set_message(TTRC("The project folder already exists and is empty."), MESSAGE_SUCCESS, target_path_input_type);
			} else {
				_set_message(TTRC("The project folder will be automatically created."), MESSAGE_SUCCESS, target_path_input_type);
			}
		} else {
			if (!d->dir_exists(target_path)) {
				_set_message(TTRC("The path specified doesn't exist."), MESSAGE_ERROR, target_path_input_type);
				return;
			}

			// The path is not necessarily empty here, but we will update the message later if it isn't.
			_set_message(TTRC("The project folder exists and is empty."), MESSAGE_SUCCESS, target_path_input_type);
		}

		// Check if the directory is empty. Not an error, but we want to warn the user.
		if (d->change_dir(target_path) == OK) {
			d->list_dir_begin();
			String n = d->get_next();
			while (!n.is_empty()) {
				if (n[0] != '.') {
					// Allow `.`, `..` (reserved current/parent folder names)
					// and hidden files/folders to be present.
					// For instance, this lets users initialize a Git repository
					// and still be able to create a project in the directory afterwards.
					is_folder_empty = false;
					break;
				}
				n = d->get_next();
			}
			d->list_dir_end();

			if (!is_folder_empty) {
				_set_message(TTRC("The selected path is not empty. Choosing an empty folder is highly recommended."), MESSAGE_WARNING, target_path_input_type);
			}
		}
	}
}

String ProjectDialog::_get_target_path() {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		return project_path->get_text();
	} else if (mode == MODE_IMPORT) {
		return install_path->get_text();
	} else {
		ERR_FAIL_V("");
	}
}
void ProjectDialog::_set_target_path(const String &p_text) {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		project_path->set_text(p_text);
	} else if (mode == MODE_IMPORT) {
		install_path->set_text(p_text);
	} else {
		ERR_FAIL();
	}
}

void ProjectDialog::_update_target_auto_dir() {
	String new_auto_dir;
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		new_auto_dir = project_name->get_text();
	} else if (mode == MODE_IMPORT) {
		new_auto_dir = project_path->get_text().get_file().get_basename();
	}
	int naming_convention = (int)EDITOR_GET("project_manager/directory_naming_convention");
	switch (naming_convention) {
		case 0: // No convention
			break;
		case 1: // kebab-case
			new_auto_dir = new_auto_dir.to_kebab_case();
			break;
		case 2: // snake_case
			new_auto_dir = new_auto_dir.to_snake_case();
			break;
		case 3: // camelCase
			new_auto_dir = new_auto_dir.to_camel_case();
			break;
		case 4: // PascalCase
			new_auto_dir = new_auto_dir.to_pascal_case();
			break;
		case 5: // Title Case
			new_auto_dir = new_auto_dir.capitalize();
			break;
		default:
			ERR_FAIL_MSG("Invalid directory naming convention.");
			break;
	}
	new_auto_dir = OS::get_singleton()->get_safe_dir_name(new_auto_dir);

	if (create_dir->is_pressed()) {
		String target_path = _get_target_path();

		if (target_path.get_file() == auto_dir) {
			// Update target dir name to new project name / ZIP name.
			target_path = target_path.get_base_dir().path_join(new_auto_dir);
		}

		_set_target_path(target_path);
	}

	auto_dir = new_auto_dir;
}

void ProjectDialog::_create_dir_toggled(bool p_pressed) {
	String target_path = _get_target_path();

	if (create_dir->is_pressed()) {
		// (Re-)append target dir name.
		if (last_custom_target_dir.is_empty()) {
			target_path = target_path.path_join(auto_dir);
		} else {
			target_path = target_path.path_join(last_custom_target_dir);
		}
	} else {
		// Strip any trailing slash.
		target_path = target_path.rstrip("/\\");
		// Save and remove target dir name.
		if (target_path.get_file() == auto_dir) {
			last_custom_target_dir = "";
		} else {
			last_custom_target_dir = target_path.get_file();
		}
		target_path = target_path.get_base_dir();
	}

	_set_target_path(target_path);
	_validate_path();
}

void ProjectDialog::_project_name_changed() {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		_update_target_auto_dir();
	}

	_validate_path();
}

void ProjectDialog::_project_path_changed() {
	if (mode == MODE_IMPORT) {
		_update_target_auto_dir();
	}

	_validate_path();
}

void ProjectDialog::_install_path_changed() {
	_validate_path();
}

void ProjectDialog::_browse_project_path() {
	String path = project_path->get_text();
	if (path.is_relative_path()) {
		path = EDITOR_GET("filesystem/directories/default_project_path");
	}
	if (mode == MODE_IMPORT && install_path->is_visible_in_tree()) {
		// Select last ZIP file.
		fdialog_project->set_current_path(path);
	} else if ((mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) && create_dir->is_pressed()) {
		// Select parent directory of project path.
		fdialog_project->set_current_dir(path.get_base_dir());
	} else {
		// Select project path.
		fdialog_project->set_current_dir(path);
	}

	if (mode == MODE_IMPORT) {
		fdialog_project->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_ANY);
		fdialog_project->clear_filters();
		fdialog_project->add_filter("project.godot", vformat("%s %s", GODOT_VERSION_NAME, TTR("Project")));
		fdialog_project->add_filter("*.zip", TTR("ZIP File"));
	} else {
		fdialog_project->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	}

	hide();
	fdialog_project->popup_file_dialog();
}

void ProjectDialog::_browse_install_path() {
	ERR_FAIL_COND_MSG(mode != MODE_IMPORT, "Install path is only used for MODE_IMPORT.");

	String path = install_path->get_text();
	if (path.is_relative_path() || !DirAccess::dir_exists_absolute(path)) {
		path = EDITOR_GET("filesystem/directories/default_project_path");
	}
	if (create_dir->is_pressed()) {
		// Select parent directory of install path.
		fdialog_install->set_current_dir(path.get_base_dir());
	} else {
		// Select install path.
		fdialog_install->set_current_dir(path);
	}

	fdialog_install->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	fdialog_install->popup_file_dialog();
}

void ProjectDialog::_project_path_selected(const String &p_path) {
	show_dialog(false);

	if (create_dir->is_pressed() && (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE)) {
		// Replace parent directory, but keep target dir name.
		project_path->set_text(p_path.path_join(project_path->get_text().get_file()));
	} else {
		project_path->set_text(p_path);
	}

	_project_path_changed();

	if (install_path->is_visible_in_tree()) {
		// ZIP is selected; focus install path.
		install_path->grab_focus();
	} else {
		get_ok_button()->grab_focus();
	}
}

void ProjectDialog::_install_path_selected(const String &p_path) {
	ERR_FAIL_COND_MSG(mode != MODE_IMPORT, "Install path is only used for MODE_IMPORT.");

	if (create_dir->is_pressed()) {
		// Replace parent directory, but keep target dir name.
		install_path->set_text(p_path.path_join(install_path->get_text().get_file()));
	} else {
		install_path->set_text(p_path);
	}

	_install_path_changed();

	get_ok_button()->grab_focus();
}

void ProjectDialog::_reset_name() {
	project_name->set_text(TTR("New Game Project"));
}

void ProjectDialog::_renderer_selected() {
	ERR_FAIL_NULL(renderer_button_group->get_pressed_button());

	String renderer_type = renderer_button_group->get_pressed_button()->get_meta(SNAME("rendering_method"));

	bool rd_error = false;

	if (renderer_type == "forward_plus") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop platforms only.") +
				String::utf8("\n•  ") + TTR("Advanced 3D graphics available.") +
				String::utf8("\n•  ") + TTR("Can scale to large complex scenes.") +
				String::utf8("\n•  ") + TTR("Uses RenderingDevice backend.") +
				String::utf8("\n•  ") + TTR("Slower rendering of simple scenes."));
		rd_error = !rendering_device_supported;
	} else if (renderer_type == "mobile") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop + mobile platforms.") +
				String::utf8("\n•  ") + TTR("Less advanced 3D graphics.") +
				String::utf8("\n•  ") + TTR("Less scalable for complex scenes.") +
				String::utf8("\n•  ") + TTR("Uses RenderingDevice backend.") +
				String::utf8("\n•  ") + TTR("Fast rendering of simple scenes."));
		rd_error = !rendering_device_supported;
	} else if (renderer_type == "gl_compatibility") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop, mobile + web platforms.") +
				String::utf8("\n•  ") + TTR("Least advanced 3D graphics.") +
				String::utf8("\n•  ") + TTR("Intended for low-end/older devices.") +
				String::utf8("\n•  ") + TTR("Uses OpenGL 3 backend (OpenGL 3.3/ES 3.0/WebGL2).") +
				String::utf8("\n•  ") + TTR("Fastest rendering of simple scenes."));
	} else {
		WARN_PRINT("Unknown renderer type. Please report this as a bug on GitHub.");
	}

	rd_not_supported->set_visible(rd_error);
	get_ok_button()->set_disabled(rd_error);
	if (rd_error) {
		// Needs to be set here since theme colors aren't available at startup.
		rd_not_supported->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
	}
}

void ProjectDialog::_nonempty_confirmation_ok_pressed() {
	is_folder_empty = true;
	ok_pressed();
}

void ProjectDialog::ok_pressed() {
	// Before we create a project, check that the target folder is empty.
	// If not, we need to ask the user if they're sure they want to do this.
	if (!is_folder_empty) {
		if (!nonempty_confirmation) {
			nonempty_confirmation = memnew(ConfirmationDialog);
			nonempty_confirmation->set_title(TTRC("Warning: This folder is not empty"));
			nonempty_confirmation->set_text(TTRC("You are about to create a Godot project in a non-empty folder.\nThe entire contents of this folder will be imported as project resources!\n\nAre you sure you wish to continue?"));
			nonempty_confirmation->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_nonempty_confirmation_ok_pressed));
			add_child(nonempty_confirmation);
		}
		nonempty_confirmation->popup_centered();
		return;
	}

	String path = project_path->get_text();

	if (mode == MODE_NEW) {
		if (create_dir->is_pressed()) {
			Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (!d->dir_exists(path) && d->make_dir(path) != OK) {
				_set_message(TTRC("Couldn't create project directory, check permissions."), MESSAGE_ERROR);
				return;
			}
		}

		PackedStringArray project_features = ProjectSettings::get_required_features();
		ProjectSettings::CustomMap initial_settings;

		// Be sure to change this code if/when renderers are changed.
		// Default values are "forward_plus" for the main setting, "mobile" for the mobile override,
		// and "gl_compatibility" for the web override.
		String renderer_type = renderer_button_group->get_pressed_button()->get_meta(SNAME("rendering_method"));
		initial_settings["rendering/renderer/rendering_method"] = renderer_type;

		EditorSettings::get_singleton()->set("project_manager/default_renderer", renderer_type);
		EditorSettings::get_singleton()->save();

		if (renderer_type == "forward_plus") {
			project_features.push_back("Forward Plus");
		} else if (renderer_type == "mobile") {
			project_features.push_back("Mobile");
		} else if (renderer_type == "gl_compatibility") {
			project_features.push_back("GL Compatibility");
			// Also change the default rendering method for the mobile override.
			initial_settings["rendering/renderer/rendering_method.mobile"] = "gl_compatibility";
		} else {
			WARN_PRINT("Unknown renderer type. Please report this as a bug on GitHub.");
		}

		project_features.sort();
		initial_settings["application/config/features"] = project_features;
		initial_settings["application/config/name"] = project_name->get_text().strip_edges();
		initial_settings["application/config/icon"] = "res://icon.svg";

		Error err = ProjectSettings::get_singleton()->save_custom(path.path_join("project.godot"), initial_settings, Vector<String>(), false);
		if (err != OK) {
			_set_message(TTRC("Couldn't create project.godot in project path."), MESSAGE_ERROR);
			return;
		}

		// Store default project icon in SVG format.
		Ref<FileAccess> fa_icon = FileAccess::open(path.path_join("icon.svg"), FileAccess::WRITE, &err);
		if (err != OK) {
			_set_message(TTRC("Couldn't create icon.svg in project path."), MESSAGE_ERROR);
			return;
		}
		fa_icon->store_string(get_default_project_icon());

		EditorVCSInterface::create_vcs_metadata_files(EditorVCSInterface::VCSMetadata(vcs_metadata_selection->get_selected()), path);

		// Ensures external editors and IDEs use UTF-8 encoding.
		const String editor_config_path = path.path_join(".editorconfig");
		Ref<FileAccess> f = FileAccess::open(editor_config_path, FileAccess::WRITE);
		if (f.is_null()) {
			// .editorconfig isn't so critical.
			ERR_PRINT("Couldn't create .editorconfig in project path.");
		} else {
			f->store_line("root = true");
			f->store_line("");
			f->store_line("[*]");
			f->store_line("charset = utf-8");
			f->close();
			FileAccess::set_hidden_attribute(editor_config_path, true);
		}
	}

	// Two cases for importing a ZIP.
	switch (mode) {
		case MODE_IMPORT: {
			if (zip_path.is_empty()) {
				break;
			}

			path = install_path->get_text().simplify_path();
			[[fallthrough]];
		}
		case MODE_INSTALL: {
			ERR_FAIL_COND(zip_path.is_empty());

			Ref<FileAccess> io_fa;
			zlib_filefunc_def io = zipio_create_io(&io_fa);

			unzFile pkg = unzOpen2(zip_path.utf8().get_data(), &io);
			if (!pkg) {
				dialog_error->set_text(TTRC("Error opening package file, not in ZIP format."));
				dialog_error->popup_centered();
				return;
			}

			// Find the first directory with a "project.godot".
			String zip_root;
			int ret = unzGoToFirstFile(pkg);
			while (ret == UNZ_OK) {
				unz_file_info info;
				char fname[16384];
				unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				if (name.get_file() == "project.godot") {
					zip_root = name.get_base_dir();
					break;
				}

				ret = unzGoToNextFile(pkg);
			}

			if (ret == UNZ_END_OF_LIST_OF_FILE) {
				_set_message(TTRC("Invalid \".zip\" project file; it doesn't contain a \"project.godot\" file."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			if (create_dir->is_pressed()) {
				Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
				if (!d->dir_exists(path) && d->make_dir(path) != OK) {
					_set_message(TTRC("Couldn't create project directory, check permissions."), MESSAGE_ERROR);
					return;
				}
			}

			ret = unzGoToFirstFile(pkg);

			Vector<String> failed_files;
			while (ret == UNZ_OK) {
				//get filename
				unz_file_info info;
				char fname[16384];
				ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				String rel_path = name.trim_prefix(zip_root);
				if (rel_path.is_empty()) { // Root.
				} else if (rel_path.ends_with("/")) { // Directory.
					Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
					da->make_dir(path.path_join(rel_path));
				} else { // File.
					Vector<uint8_t> uncomp_data;
					uncomp_data.resize(info.uncompressed_size);

					unzOpenCurrentFile(pkg);
					ret = unzReadCurrentFile(pkg, uncomp_data.ptrw(), uncomp_data.size());
					ERR_BREAK_MSG(ret < 0, vformat("An error occurred while attempting to read from file: %s. This file will not be used.", rel_path));
					unzCloseCurrentFile(pkg);

					Ref<FileAccess> f = FileAccess::open(path.path_join(rel_path), FileAccess::WRITE);
					if (f.is_valid()) {
						f->store_buffer(uncomp_data.ptr(), uncomp_data.size());
					} else {
						failed_files.push_back(rel_path);
					}
				}

				ret = unzGoToNextFile(pkg);
			}

			unzClose(pkg);

			if (failed_files.size()) {
				String err_msg = TTR("The following files failed extraction from package:") + "\n\n";
				for (int i = 0; i < failed_files.size(); i++) {
					if (i > 15) {
						err_msg += "\nAnd " + itos(failed_files.size() - i) + " more files.";
						break;
					}
					err_msg += failed_files[i] + "\n";
				}

				dialog_error->set_text(err_msg);
				dialog_error->popup_centered();
				return;
			}
		} break;
		default: {
		} break;
	}

	if (mode == MODE_DUPLICATE) {
		Ref<DirAccess> dir = DirAccess::open(original_project_path);
		Error err = FAILED;
		if (dir.is_valid()) {
			err = dir->copy_dir(".", path, -1, true);
		}
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't duplicate project (error %d)."), err));
			dialog_error->popup_centered();
			return;
		}
	}

	if (mode == MODE_RENAME || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		// Load project.godot as ConfigFile to set the new name.
		ConfigFile cfg;
		String project_godot = path.path_join("project.godot");
		Error err = cfg.load(project_godot);
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't load project at '%s' (error %d). It may be missing or corrupted."), project_godot, err));
			dialog_error->popup_centered();
			return;
		}
		cfg.set_value("application", "config/name", project_name->get_text().strip_edges());
		err = cfg.save(project_godot);
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't save project at '%s' (error %d)."), project_godot, err));
			dialog_error->popup_centered();
			return;
		}
	}

	hide();
	if (mode == MODE_NEW || mode == MODE_IMPORT || mode == MODE_INSTALL) {
#ifdef ANDROID_ENABLED
		// Create a .nomedia file to hide assets from media apps on Android.
		// Android 11 has some issues with nomedia files, so it's disabled there. See GH-106479, GH-105399 for details.
		// NOTE: Nomedia file is also handled during the first filesystem scan. See editor_file_system.cpp -> EditorFileSystem::scan().
		String sdk_version = OS::get_singleton()->get_version().get_slicec('.', 0);
		if (sdk_version != "30") {
			const String nomedia_file_path = path.path_join(".nomedia");
			Ref<FileAccess> f2 = FileAccess::open(nomedia_file_path, FileAccess::WRITE);
			if (f2.is_null()) {
				// .nomedia isn't so critical.
				ERR_PRINT("Couldn't create .nomedia in project path.");
			} else {
				f2->close();
			}
		}
#endif
		emit_signal(SNAME("project_created"), path, edit_check_box->is_pressed());
	} else if (mode == MODE_DUPLICATE) {
		emit_signal(SNAME("project_duplicated"), original_project_path, path, edit_check_box->is_visible() && edit_check_box->is_pressed());
	} else if (mode == MODE_RENAME) {
		emit_signal(SNAME("projects_updated"));
	}
}

void ProjectDialog::set_zip_path(const String &p_path) {
	zip_path = p_path;
}

void ProjectDialog::set_zip_title(const String &p_title) {
	zip_title = p_title;
}

void ProjectDialog::set_original_project_path(const String &p_path) {
	original_project_path = p_path;
}

void ProjectDialog::set_duplicate_can_edit(bool p_duplicate_can_edit) {
	duplicate_can_edit = p_duplicate_can_edit;
}

void ProjectDialog::set_mode(Mode p_mode) {
	mode = p_mode;
}

void ProjectDialog::set_project_name(const String &p_name) {
	project_name->set_text(p_name);
}

void ProjectDialog::set_project_path(const String &p_path) {
	project_path->set_text(p_path);
}

void ProjectDialog::ask_for_path_and_show() {
	_reset_name();
	_browse_project_path();
}

void ProjectDialog::show_dialog(bool p_reset_name) {
	if (mode == MODE_RENAME) {
		// Name and path are set in `ProjectManager::_rename_project`.
		project_path->set_editable(false);

		set_title(TTRC("Rename Project"));
		set_ok_button_text(TTRC("Rename"));

		create_dir->hide();
		project_status_rect->hide();
		project_browse->hide();
		edit_check_box->hide();
		doro_container->hide(); // DORO: Hide mode selection UI
		get_ok_button()->show(); // DORO: Show OK button

		name_container->show();
		install_path_container->hide();
		renderer_container->hide();
		default_files_container->hide();

		callable_mp((Control *)project_name, &Control::grab_focus).call_deferred();
		callable_mp(project_name, &LineEdit::select_all).call_deferred();
	} else {
		if (p_reset_name) {
			_reset_name();
		}
		project_path->set_editable(true);

		if (mode == MODE_DUPLICATE) {
			String original_dir = original_project_path.get_base_dir();
			project_path->set_text(original_dir);
			install_path->set_text(original_dir);
			fdialog_project->set_current_dir(original_dir);
		} else {
			String fav_dir = EDITOR_GET("filesystem/directories/default_project_path");
			fav_dir = fav_dir.simplify_path();
			if (!fav_dir.is_empty()) {
				project_path->set_text(fav_dir);
				install_path->set_text(fav_dir);
				fdialog_project->set_current_dir(fav_dir);
			} else {
				Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
				project_path->set_text(d->get_current_dir());
				install_path->set_text(d->get_current_dir());
				fdialog_project->set_current_dir(d->get_current_dir());
			}
		}

		create_dir->show();
		project_status_rect->show();
		project_browse->show();
		edit_check_box->show();

		if (mode == MODE_IMPORT) {
			set_title(TTRC("Import Existing Project"));
			set_ok_button_text(TTRC("Import"));

			name_container->hide();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();

			// Project path dialog is also opened; no need to change focus.
		} else if (mode == MODE_NEW) {
			// DORO: RADICAL - Override ALL Window styles to be transparent
			// The doro_overlay inside provides the semi-transparent background

			set_title(U""); // Empty title
			get_ok_button()->hide();
			get_cancel_button()->hide();

			// DORO: Make window fullscreen
			Size2 screen_size = DisplayServer::get_singleton()->window_get_size();
			set_size(screen_size);
			set_position(Point2(0, 0));

			// DORO: Create TRANSPARENT style for ALL window parts
			Ref<StyleBoxFlat> transparent;
			transparent.instantiate();
			transparent->set_bg_color(Color(0, 0, 0, 0)); // FULLY TRANSPARENT
			transparent->set_border_width_all(0);
			transparent->set_content_margin_all(0);

			// Override EVERYTHING
			add_theme_style_override("panel", transparent);
			add_theme_style_override("embedded_border", transparent);
			add_theme_style_override("embedded_unfocused_border", transparent);

			// Hide close button
			set_flag(Window::FLAG_NO_FOCUS, false);

			// DORO: Show the internal overlay (this provides semi-transparent background)
			if (doro_overlay) {
				doro_overlay->show();
			}

			// Show DORO UI
			doro_container->show();
			doro_project_name->set_text(U"");
			doro_input_placeholder = doro_project_name->get_placeholder(); // Store placeholder
			_update_doro_card_styles();

			// Hide default UI elements
			name_container->hide();
			project_path_container->hide();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();
			create_dir->hide();
			project_browse->hide();
			edit_check_box->hide();
			msg->hide(); // Hide error/status message

			// Don't auto-focus the input to avoid showing cursor on placeholder
		} else if (mode == MODE_INSTALL) {
			set_title(TTR("Install Project:") + " " + zip_title);
			set_ok_button_text(TTRC("Install"));

			project_name->set_text(zip_title);

			name_container->show();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();

			callable_mp((Control *)project_path, &Control::grab_focus).call_deferred();
		} else if (mode == MODE_DUPLICATE) {
			set_title(TTRC("Duplicate Project"));
			set_ok_button_text(TTRC("Duplicate"));

			name_container->show();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();
			if (!duplicate_can_edit) {
				edit_check_box->hide();
			}

			callable_mp((Control *)project_name, &Control::grab_focus).call_deferred();
			callable_mp(project_name, &LineEdit::select_all).call_deferred();
		}

		auto_dir = "";
		last_custom_target_dir = "";
		_update_target_auto_dir();
		if (create_dir->is_pressed()) {
			// Append `auto_dir` to target path.
			_create_dir_toggled(true);
		}
	}

	_validate_path();

	// DORO: Larger modal for MODE_NEW with mode selection
	if (mode == MODE_NEW) {
		popup_centered(Size2(700, 0) * EDSCALE); // Compact modal for mode selection
	} else {
		popup_centered(Size2(500, 0) * EDSCALE);
	}
}

void ProjectDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED: {
			_renderer_selected();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			create_dir->set_button_icon(get_editor_theme_icon(SNAME("FolderCreate")));
			project_browse->set_button_icon(get_editor_theme_icon(SNAME("FolderBrowse")));
			install_browse->set_button_icon(get_editor_theme_icon(SNAME("FolderBrowse")));
		} break;
		case NOTIFICATION_READY: {
			fdialog_project = memnew(EditorFileDialog);
			fdialog_project->set_previews_enabled(false);
			fdialog_project->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			fdialog_project->connect("dir_selected", callable_mp(this, &ProjectDialog::_project_path_selected));
			fdialog_project->connect("file_selected", callable_mp(this, &ProjectDialog::_project_path_selected));
			fdialog_project->connect("canceled", callable_mp(this, &ProjectDialog::show_dialog).bind(false), CONNECT_DEFERRED);
			callable_mp((Node *)this, &Node::add_sibling).call_deferred(fdialog_project, false);
			// Note: doro_overlay is now created in constructor
		} break;
	}
}

// DORO: Public method to trigger project creation
void ProjectDialog::create_project() {
	ok_pressed();
}

void ProjectDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("project_created"));
	ADD_SIGNAL(MethodInfo("project_duplicated"));
	ADD_SIGNAL(MethodInfo("projects_updated"));
}

ProjectDialog::ProjectDialog() {
	// DORO: RADICAL RESTRUCTURE - overlay fills the whole dialog, content is centered inside

	// 1. Create dark overlay FIRST - covers entire dialog area
	doro_overlay = memnew(ColorRect);
	doro_overlay->set_color(Color(0.0, 0.0, 0.0, 0.5)); // Semi-transparent black
	doro_overlay->set_anchors_preset(Control::PRESET_FULL_RECT);
	doro_overlay->set_mouse_filter(Control::MOUSE_FILTER_STOP); // Capture clicks
	doro_overlay->hide(); // Initially hidden
	doro_overlay->connect("gui_input", callable_mp(this, &ProjectDialog::_doro_overlay_clicked).unbind(1));
	add_child(doro_overlay);

	// 2. CenterContainer to center the content panel
	CenterContainer *center = memnew(CenterContainer);
	center->set_anchors_preset(Control::PRESET_FULL_RECT);
	center->set_mouse_filter(Control::MOUSE_FILTER_PASS); // Pass to children, not overlay
	add_child(center);

	// 3. Main content VBoxContainer inside CenterContainer
	VBoxContainer *vb = memnew(VBoxContainer);
	vb->set_mouse_filter(Control::MOUSE_FILTER_PASS); // Pass to children
	center->add_child(vb);

	// DORO: Mode selection UI (only shown for MODE_NEW)
	{
		// DORO: Light background wrapper for the entire modal content
		PanelContainer *doro_bg_panel = memnew(PanelContainer);
		{
			Ref<StyleBoxFlat> bg_style;
			bg_style.instantiate();
			bg_style->set_bg_color(Color(0.96, 0.97, 0.99)); // Very light gray
			bg_style->set_corner_radius_all(12);
			bg_style->set_content_margin_all(16); // Normal padding
			doro_bg_panel->add_theme_style_override("panel", bg_style);
		}
		doro_bg_panel->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
		vb->add_child(doro_bg_panel);

		doro_container = memnew(VBoxContainer);
		doro_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		doro_container->add_theme_constant_override("separation", 16 * EDSCALE);
		doro_bg_panel->add_child(doro_container);

		// Title: "무엇을 만들어볼까요?"
		Label *doro_title = memnew(Label);
		doro_title->set_text(U"무엇을 만들어볼까요?");
		doro_title->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
		doro_title->add_theme_font_size_override(SceneStringName(font_size), 24 * EDSCALE);
		doro_title->add_theme_color_override(SceneStringName(font_color), Color(0.10, 0.21, 0.36)); // #1A365D dark blue
		doro_container->add_child(doro_title);

		// Mode cards container (horizontal)
		HBoxContainer *cards_hbox = memnew(HBoxContainer);
		cards_hbox->set_alignment(BoxContainer::ALIGNMENT_CENTER);
		cards_hbox->add_theme_constant_override("separation", 20 * EDSCALE); // Wider spacing
		doro_container->add_child(cards_hbox);

		// Mode card data
		const char *mode_names[] = { "MODDER", "BUILDER", "BRIDGER", "HACKER" };
		const char32_t *mode_descs[] = {
			U"숫자를 바꿔가며\n게임을 고쳐봐요",
			U"이벤트와 조건으로\n게임을 만들어요",
			U"블록과 코드가\n서로 연결돼요",
			U"진짜 코드로\n자유롭게 개발해요"
		};
		// DORO: Mode-specific colors
		const Color mode_colors[] = {
			Color(0.30, 0.55, 0.85), // MODDER: Blue
			Color(0.20, 0.70, 0.45), // BUILDER: Green
			Color(0.75, 0.55, 0.25), // BRIDGER: Orange
			Color(0.65, 0.35, 0.70) // HACKER: Purple
		};

		for (int i = 0; i < 4; i++) {
			// DORO: Use Control as container with fixed size, then put PanelContainer inside
			Control *card_wrapper = memnew(Control);
			card_wrapper->set_custom_minimum_size(Size2(120, 80) * EDSCALE); // Reasonable card size
			card_wrapper->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
			card_wrapper->set_v_size_flags(Control::SIZE_SHRINK_CENTER);

			PanelContainer *card = memnew(PanelContainer);
			card->set_anchors_preset(Control::PRESET_FULL_RECT);
			card->set_clip_contents(true);

			// Card style - white with subtle border
			Ref<StyleBoxFlat> card_style;
			card_style.instantiate();
			card_style->set_bg_color(Color(1.0, 1.0, 1.0));
			card_style->set_corner_radius_all(8);
			card_style->set_border_width_all(2);
			card_style->set_border_color(Color(0.85, 0.88, 0.92));
			card_style->set_content_margin_all(8);
			card->add_theme_style_override("panel", card_style);

			VBoxContainer *card_content = memnew(VBoxContainer);
			card_content->set_alignment(BoxContainer::ALIGNMENT_CENTER);
			card_content->add_theme_constant_override("separation", 4 * EDSCALE);
			card->add_child(card_content);

			// Mode name - smaller font to fit in card
			Label *name_label = memnew(Label);
			name_label->set_text(mode_names[i]);
			name_label->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
			name_label->add_theme_font_size_override(SceneStringName(font_size), 14 * EDSCALE);
			name_label->add_theme_color_override(SceneStringName(font_color), mode_colors[i]); // Mode-specific color
			card_content->add_child(name_label);

			// Mode description - much smaller font
			Label *desc_label = memnew(Label);
			desc_label->set_text(String(mode_descs[i]));
			desc_label->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
			desc_label->add_theme_font_size_override(SceneStringName(font_size), 10 * EDSCALE);
			desc_label->add_theme_color_override(SceneStringName(font_color), Color(0.35, 0.40, 0.50)); // Darker gray
			card_content->add_child(desc_label);

			doro_mode_cards[i] = card;
			card_wrapper->add_child(card);
			cards_hbox->add_child(card_wrapper);

			// Click handler using gui_input - unbind the InputEvent, bind the index
			card->connect("gui_input", callable_mp(this, &ProjectDialog::_doro_mode_selected).unbind(1).bind(i));
		}

		// Project name input
		VBoxContainer *name_input_vbox = memnew(VBoxContainer);
		name_input_vbox->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
		doro_container->add_child(name_input_vbox);

		doro_project_name = memnew(LineEdit);
		doro_project_name->set_placeholder(U"선택한 모드로 만들 작품 이름을 입력하세요");
		doro_project_name->set_custom_minimum_size(Size2(400 * EDSCALE, 36 * EDSCALE)); // Very compact

		// DORO: White background style for input - normal state
		{
			Ref<StyleBoxFlat> input_normal;
			input_normal.instantiate();
			input_normal->set_bg_color(Color(1.0, 1.0, 1.0)); // White
			input_normal->set_corner_radius_all(8);
			input_normal->set_border_width_all(1);
			input_normal->set_border_color(Color(0.85, 0.88, 0.92)); // Gray border
			input_normal->set_content_margin(Side::SIDE_LEFT, 16);
			input_normal->set_content_margin(Side::SIDE_RIGHT, 16);
			input_normal->set_content_margin(Side::SIDE_TOP, 12);
			input_normal->set_content_margin(Side::SIDE_BOTTOM, 12);
			doro_project_name->add_theme_style_override("normal", input_normal);

			// Focus state - blue border for visual feedback
			Ref<StyleBoxFlat> input_focus;
			input_focus.instantiate();
			input_focus->set_bg_color(Color(1.0, 1.0, 1.0));
			input_focus->set_corner_radius_all(8);
			input_focus->set_border_width_all(2);
			input_focus->set_border_color(Color(0.35, 0.60, 0.85)); // Blue border on focus
			input_focus->set_content_margin(Side::SIDE_LEFT, 16);
			input_focus->set_content_margin(Side::SIDE_RIGHT, 16);
			input_focus->set_content_margin(Side::SIDE_TOP, 12);
			input_focus->set_content_margin(Side::SIDE_BOTTOM, 12);
			doro_project_name->add_theme_style_override("focus", input_focus);
		}
		doro_project_name->add_theme_color_override("font_color", Color(0.2, 0.25, 0.3));
		doro_project_name->add_theme_color_override("font_placeholder_color", Color(0.5, 0.55, 0.6));
		doro_project_name->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
		doro_project_name->set_focus_mode(Control::FOCUS_CLICK);
		// DORO: Connect gui_input to hide placeholder on click (Web compatible)
		doro_project_name->connect("gui_input", callable_mp(this, &ProjectDialog::_doro_input_gui_input));
		name_input_vbox->add_child(doro_project_name);

		// Start button
		doro_start_button = memnew(Button);
		doro_start_button->set_text(U"이 모드로 시작하기");
		doro_start_button->set_custom_minimum_size(Size2(280 * EDSCALE, 52 * EDSCALE)); // Slightly larger
		doro_start_button->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
		doro_start_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_doro_start_pressed));

		// Button style (blue pill - matching original design color)
		Ref<StyleBoxFlat> btn_style;
		btn_style.instantiate();
		btn_style->set_bg_color(Color(0.35, 0.60, 0.85)); // #5A9AD9 brighter blue like original
		btn_style->set_corner_radius_all(26);
		btn_style->set_content_margin(Side::SIDE_LEFT, 40);
		btn_style->set_content_margin(Side::SIDE_RIGHT, 40);
		btn_style->set_content_margin(Side::SIDE_TOP, 14);
		btn_style->set_content_margin(Side::SIDE_BOTTOM, 14);
		doro_start_button->add_theme_style_override("normal", btn_style);
		doro_start_button->add_theme_style_override("hover", btn_style);
		doro_start_button->add_theme_style_override("pressed", btn_style);
		doro_start_button->add_theme_style_override("focus", btn_style); // Remove focus border
		doro_start_button->set_focus_mode(Control::FOCUS_NONE); // Disable focus entirely
		doro_start_button->add_theme_color_override(SceneStringName(font_color), Color(1.0, 1.0, 1.0));
		doro_start_button->add_theme_font_size_override(SceneStringName(font_size), 20 * EDSCALE);

		doro_container->add_child(doro_start_button);

		// Initially hidden (will be shown in show_dialog for MODE_NEW)
		doro_container->hide();
	}

	name_container = memnew(VBoxContainer);
	vb->add_child(name_container);

	Label *l = memnew(Label);
	l->set_text(TTRC("Project Name:"));
	name_container->add_child(l);

	project_name = memnew(LineEdit);
	project_name->set_virtual_keyboard_show_on_focus(false);
	project_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	name_container->add_child(project_name);

	project_path_container = memnew(VBoxContainer);
	vb->add_child(project_path_container);

	HBoxContainer *pphb_label = memnew(HBoxContainer);
	project_path_container->add_child(pphb_label);

	l = memnew(Label);
	l->set_text(TTRC("Project Path:"));
	l->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	pphb_label->add_child(l);

	create_dir = memnew(CheckButton);
	create_dir->set_text(TTRC("Create Folder"));
	create_dir->set_pressed(true);
	pphb_label->add_child(create_dir);
	create_dir->connect(SceneStringName(toggled), callable_mp(this, &ProjectDialog::_create_dir_toggled));

	HBoxContainer *pphb = memnew(HBoxContainer);
	project_path_container->add_child(pphb);

	project_path = memnew(LineEdit);
	project_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	project_path->set_accessibility_name(TTRC("Project Path:"));
	project_path->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	pphb->add_child(project_path);

	install_path_container = memnew(VBoxContainer);
	vb->add_child(install_path_container);

	l = memnew(Label);
	l->set_text(TTRC("Project Installation Path:"));
	install_path_container->add_child(l);

	HBoxContainer *iphb = memnew(HBoxContainer);
	install_path_container->add_child(iphb);

	install_path = memnew(LineEdit);
	install_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	install_path->set_accessibility_name(TTRC("Project Installation Path:"));
	install_path->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	iphb->add_child(install_path);

	// status icon
	project_status_rect = memnew(TextureRect);
	project_status_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	pphb->add_child(project_status_rect);

	project_browse = memnew(Button);
	project_browse->set_text(TTRC("Browse"));
	project_browse->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_browse_project_path));
	pphb->add_child(project_browse);

	// install status icon
	install_status_rect = memnew(TextureRect);
	install_status_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	iphb->add_child(install_status_rect);

	install_browse = memnew(Button);
	install_browse->set_text(TTRC("Browse"));
	install_browse->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_browse_install_path));
	iphb->add_child(install_browse);

	msg = memnew(Label);
	msg->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	msg->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	msg->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	msg->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	vb->add_child(msg);

	// Renderer selection.
	renderer_container = memnew(VBoxContainer);
	vb->add_child(renderer_container);
	l = memnew(Label);
	l->set_text(TTRC("Renderer:"));
	renderer_container->add_child(l);
	HBoxContainer *rshc = memnew(HBoxContainer);
	renderer_container->add_child(rshc);
	renderer_button_group.instantiate();

	// Left hand side, used for checkboxes to select renderer.
	Container *rvb = memnew(VBoxContainer);
	rshc->add_child(rvb);

	String default_renderer_type = "forward_plus";
	if (EditorSettings::get_singleton()->has_setting("project_manager/default_renderer")) {
		default_renderer_type = EditorSettings::get_singleton()->get_setting("project_manager/default_renderer");
	}

	rendering_device_supported = DisplayServer::is_rendering_device_supported();

	if (!rendering_device_supported) {
		default_renderer_type = "gl_compatibility";
	}

	Button *rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Forward+"));
#ifndef RD_ENABLED
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "forward_plus");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
	if (default_renderer_type == "forward_plus") {
		rs_button->set_pressed(true);
	}
	rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Mobile"));
#ifndef RD_ENABLED
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "mobile");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
	if (default_renderer_type == "mobile") {
		rs_button->set_pressed(true);
	}
	rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Compatibility"));
#if !defined(GLES3_ENABLED)
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "gl_compatibility");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
#if defined(GLES3_ENABLED)
	if (default_renderer_type == "gl_compatibility") {
		rs_button->set_pressed(true);
	}
#endif
	rshc->add_child(memnew(VSeparator));

	// Right hand side, used for text explaining each choice.
	rvb = memnew(VBoxContainer);
	rvb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rshc->add_child(rvb);
	renderer_info = memnew(Label);
	renderer_info->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	renderer_info->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	renderer_info->set_modulate(Color(1, 1, 1, 0.7));
	rvb->add_child(renderer_info);

	rd_not_supported = memnew(Label);
	rd_not_supported->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	rd_not_supported->set_text(vformat(TTRC("RenderingDevice-based methods not available on this GPU:\n%s\nPlease use the Compatibility renderer."), RenderingServer::get_singleton()->get_video_adapter_name()));
	rd_not_supported->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	rd_not_supported->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	rd_not_supported->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	rd_not_supported->set_visible(false);
	renderer_container->add_child(rd_not_supported);

	_renderer_selected();

	l = memnew(Label);
	l->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	l->set_text(TTRC("The renderer can be changed later, but scenes may need to be adjusted."));
	// Add some extra spacing to separate it from the list above and the buttons below.
	l->set_custom_minimum_size(Size2(0, 40) * EDSCALE);
	l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	l->set_modulate(Color(1, 1, 1, 0.7));
	renderer_container->add_child(l);

	default_files_container = memnew(HBoxContainer);
	vb->add_child(default_files_container);
	l = memnew(Label);
	l->set_text(TTRC("Version Control Metadata:"));
	default_files_container->add_child(l);
	vcs_metadata_selection = memnew(OptionButton);
	vcs_metadata_selection->set_custom_minimum_size(Size2(100, 20));
	vcs_metadata_selection->add_item(TTRC("None"), (int)EditorVCSInterface::VCSMetadata::NONE);
	vcs_metadata_selection->add_item(TTRC("Git"), (int)EditorVCSInterface::VCSMetadata::GIT);
	vcs_metadata_selection->select((int)EditorVCSInterface::VCSMetadata::GIT);
	vcs_metadata_selection->set_accessibility_name(TTRC("Version Control Metadata:"));
	default_files_container->add_child(vcs_metadata_selection);
	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	default_files_container->add_child(spacer);
	fdialog_install = memnew(EditorFileDialog);
	fdialog_install->set_previews_enabled(false); //Crucial, otherwise the engine crashes.
	fdialog_install->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	add_child(fdialog_install);

	Control *spacer2 = memnew(Control);
	spacer2->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vb->add_child(spacer2);

	edit_check_box = memnew(CheckBox);
	edit_check_box->set_text(TTRC("Edit Now"));
	edit_check_box->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	edit_check_box->set_pressed(true);
	vb->add_child(edit_check_box);

	project_name->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_project_name_changed).unbind(1));
	project_name->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	project_path->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_project_path_changed).unbind(1));
	project_path->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	install_path->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_install_path_changed).unbind(1));
	install_path->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	fdialog_install->connect("dir_selected", callable_mp(this, &ProjectDialog::_install_path_selected));
	fdialog_install->connect("file_selected", callable_mp(this, &ProjectDialog::_install_path_selected));

	set_hide_on_ok(false);

	dialog_error = memnew(AcceptDialog);
	add_child(dialog_error);
}
