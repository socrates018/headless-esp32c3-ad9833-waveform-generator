Import("env")

def after_upload(source, target, env):
    upload_port = env.subst("$UPLOAD_PORT")
    if upload_port:
        env.Execute(f"pio run -t uploadfs --upload-port {upload_port}")
    else:
        env.Execute("pio run -t uploadfs")

env.AddPostAction("upload", after_upload)
