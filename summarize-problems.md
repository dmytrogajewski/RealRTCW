Run command: `/home/dmitriy/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RealRTCW/start_native.sh`

Observed issues from latest run:
[] AI script fatal error: `AI Scripting: syntax error` at directive `playanim <animation> <legs/torso/both>` leading to server crash during `dam` load (with many “client already shutdown” follow-ons).
[] Renderer warnings: missing closing brace in shader definitions `textures/sfx/portal3a_back2` and `models/mapobjects/flag/flag_dam` during shader parse.
[] Shader parameter warnings: multiple models reference unknown general shader parameter `picmip2`.
[] Input config warnings: unknown commands `in_joystick`, `in_restart`, `in_joystickuseanalog` from cfg execution.
[] Windowing warning: `libdecor-gtk-WARNING: Failed to initialize GTK` when creating SDL Wayland window.
[] GPU capability note: `GL_EXT_texture_compression_s3tc` not found (S3TC compression disabled).

