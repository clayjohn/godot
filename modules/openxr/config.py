def can_build(env, platform):
    if platform in ("linuxbsd", "windows"):
        return env["openxr"]
    elif platform == "android" and not env["opengl3"]:
        return env["openxr"]
    else:
        # not supported on these platforms
        return False


def configure(env):
    pass


def get_doc_classes():
    return [
        "OpenXRInterface",
        "OpenXRAction",
        "OpenXRActionSet",
        "OpenXRActionMap",
        "OpenXRInteractionProfile",
        "OpenXRIPBinding",
        "OpenXRHand",
    ]


def get_doc_path():
    return "doc_classes"
