import hashlib
import os
import re
from datetime import datetime
from pathlib import Path

from fastapi import FastAPI, File, Form, Header, UploadFile
from fastapi.responses import JSONResponse

app = FastAPI(title="TCU Log Upload Server")

UPLOAD_TOKEN = os.getenv("UPLOAD_TOKEN", "test-token-001")
UPLOAD_ROOT = Path(os.getenv("UPLOAD_ROOT", "/data/uploads"))
MAX_UPLOAD_MB = int(os.getenv("MAX_UPLOAD_MB", "100"))
MAX_UPLOAD_BYTES = MAX_UPLOAD_MB * 1024 * 1024

SAFE_DEVICE_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
SAFE_FILE_RE = re.compile(r"^[A-Za-z0-9_.-]+\.tar\.gz$")


def fail(code, message, http_status=400):
    return JSONResponse(
        status_code=http_status,
        content={"code": code, "message": message},
    )


@app.get("/health")
def health():
    return {"code": 0, "message": "ok"}


@app.post("/api/device/log/upload")
async def upload_log(
    file: UploadFile = File(...),
    device_id: str = Form(...),
    timestamp: str = Form(None),
    checksum: str = Form(None),
    authorization: str = Header(None),
):
    if authorization != f"Bearer {UPLOAD_TOKEN}":
        return fail(4001, "invalid token", 401)

    if not SAFE_DEVICE_RE.match(device_id):
        return fail(4002, "invalid device_id")

    filename = Path(file.filename or "").name
    if not SAFE_FILE_RE.match(filename):
        return fail(4003, "only .tar.gz file is allowed")

    now = datetime.now()
    day_dir = UPLOAD_ROOT / device_id / now.strftime("%Y%m") / now.strftime("%d")
    day_dir.mkdir(parents=True, exist_ok=True)

    final_path = day_dir / filename
    tmp_path = day_dir / f".{filename}.tmp"

    sha256 = hashlib.sha256()
    total = 0

    with tmp_path.open("wb") as out:
        while True:
            chunk = await file.read(1024 * 1024)
            if not chunk:
                break

            total += len(chunk)
            if total > MAX_UPLOAD_BYTES:
                out.close()
                tmp_path.unlink(missing_ok=True)
                return fail(4004, f"file too large, max {MAX_UPLOAD_MB}MB", 413)

            sha256.update(chunk)
            out.write(chunk)

    actual_checksum = sha256.hexdigest()

    if checksum and checksum.lower() != actual_checksum:
        tmp_path.unlink(missing_ok=True)
        return fail(4005, "checksum mismatch")

    duplicated = final_path.exists()
    if duplicated:
        tmp_path.unlink(missing_ok=True)
    else:
        tmp_path.rename(final_path)

    file_id = str(final_path.relative_to(UPLOAD_ROOT))

    return {
        "code": 0,
        "message": "ok",
        "file_id": file_id,
        "duplicated": duplicated,
        "size": total,
        "sha256": actual_checksum,
        "timestamp": timestamp,
    }
