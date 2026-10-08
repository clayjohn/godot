def can_build(env, platform):
    return env.editor_build or env["icbc_export_templates"]


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [
        BoolVariable(
            "icbc_export_templates",
            "Enable ICBC BC1 image compression in export template builds (increases binary size)",
            False,
        ),
    ]


def configure(env):
    pass
