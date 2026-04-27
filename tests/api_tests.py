import os
import time
from urllib.parse import parse_qs, urlparse

import pytest
import requests


BASE_URL = os.getenv("BASE_URL", "http://localhost:8080")
ANDREW_EMAIL = "andrew@test.com"
ANDREW_PASSWORD = os.getenv("ANDREW_PASSWORD", "hash3")
FALLBACK_TOKEN = "56b99f47c711f0f3ed7ced1b83565f7d3488e5fbfe7d8cd3910760765ddfdcca"
SESSION_TOKEN = os.getenv("API_TOKEN")
WRONG_TOKEN = "0" * 64
RESOURCE_ID_BASE = int(time.time()) % 1_000_000 + 100_000
TOKENS_TO_LOGOUT = set()


@pytest.fixture(scope="session", autouse=True)
def logout_created_sessions():
    yield

    for token in TOKENS_TO_LOGOUT:
        requests.post(f"{BASE_URL}/api/logout", headers=auth_headers(token))


def auth_headers(token):
    return {"Authorization": f"Bearer {token}"}


def remember_token(token):
    TOKENS_TO_LOGOUT.add(token)
    return token


def api_delete_resource(resource_id, token=None):
    return requests.delete(
        f"{BASE_URL}/api/resources?id={resource_id}",
        headers=auth_headers(token or get_session_token()),
    )


def assert_json_error(response, status_code, message):
    assert response.status_code == status_code
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"error": message}


def assert_json_has_error(response, status_code):
    assert response.status_code == status_code
    assert response.headers["content-type"].startswith("application/json")
    assert "error" in response.json()


def test_01_login_missing_fields_returns_error():
    response = requests.post(f"{BASE_URL}/api/login", json={})

    assert_json_error(response, 400, "missing fields")


def test_02_login_only_with_email_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL},
    )

    assert_json_error(response, 400, "missing fields")


def test_03_login_wrong_email_correct_password_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": "missing@test.com", "password": ANDREW_PASSWORD},
    )

    assert_json_error(response, 400, "user not found")


def test_04_login_wrong_password_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": "wrong-password"},
    )

    assert_json_error(response, 400, "wrong password")


def test_05_login_valid_user_returns_token():
    global SESSION_TOKEN

    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": ANDREW_PASSWORD},
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")

    body = response.json()
    assert body["user_id"] == 1
    assert isinstance(body["token"], str)
    assert len(body["token"]) == 64
    remember_token(body["token"])

    if SESSION_TOKEN is None:
        SESSION_TOKEN = body["token"]


def get_session_token():
    return SESSION_TOKEN or FALLBACK_TOKEN


def test_06_upload_url_requires_token():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        json={"filename": "report.pdf", "content_type": "application/pdf"},
    )

    assert response.status_code == 403


def test_07_upload_url_missing_fields_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={"filename": "report.pdf"},
    )

    assert_json_error(response, 400, "missing fields")


def test_08_upload_url_valid_request_returns_presigned_url():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={
            "filename": "report.pdf",
            "content_type": "application/pdf",
        },
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")

    body = response.json()
    assert body["bucket"] == "webserver-files"
    assert body["content_type"] == "application/pdf"
    assert body["object_key"].startswith("users/1/uploads/")
    assert body["object_key"].endswith("-report.pdf")
    assert body["public_url"] == f"http://localhost:9000/webserver-files/{body['object_key']}"

    parsed = urlparse(body["upload_url"])
    query = parse_qs(parsed.query)

    assert parsed.scheme == "http"
    assert parsed.netloc == "localhost:9000"
    assert parsed.path == f"/webserver-files/{body['object_key']}"
    assert query["X-Amz-Algorithm"] == ["AWS4-HMAC-SHA256"]
    assert query["X-Amz-Expires"] == ["300"]
    assert query["X-Amz-SignedHeaders"] == ["host"]
    assert "X-Amz-Credential" in query
    assert "X-Amz-Date" in query
    assert "X-Amz-Signature" in query


def test_09_get_resources_requires_token():
    response = requests.get(f"{BASE_URL}/api/resources")

    assert response.status_code == 403


def test_10_get_resources_rejects_wrong_token():
    response = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
    )

    assert response.status_code == 403


def test_11_get_resources_for_andrew():
    response = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")

    body = response.json()
    assert "data" in body

    resources = {item["id"]: item for item in body["data"]}
    assert resources[1] == {
        "id": 1,
        "title": "First resource",
        "content": "hello world",
        "is_file": False,
    }
    assert resources[2] == {
        "id": 2,
        "title": "Second resource",
        "content": "hello People",
        "is_file": False,
    }


def test_12_post_resource_skipping_id_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={"title": "Missing id", "content": "This should fail"},
    )

    assert_json_error(response, 400, "invalid id")


def test_13_post_resource_with_wrong_token_is_forbidden():
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
        json={
            "id": RESOURCE_ID_BASE + 10,
            "title": "Wrong token",
            "content": "This should fail",
        },
    )

    assert response.status_code == 403


def test_14_post_resource_skipping_title_uses_current_api_behavior():
    resource_id = RESOURCE_ID_BASE + 11

    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={"id": resource_id, "content": "Created without title"},
    )

    try:
        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "created"}
    finally:
        api_delete_resource(resource_id)


def test_15_post_resource_skipping_content_uses_current_api_behavior():
    resource_id = RESOURCE_ID_BASE + 12

    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={"id": resource_id, "title": "Created without content"},
    )

    try:
        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "created"}
    finally:
        api_delete_resource(resource_id)


def test_16_post_resource_with_full_message_creates_resource():
    resource_id = RESOURCE_ID_BASE + 13

    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "API test resource",
            "content": "Created by pytest",
            "is_file": False,
        },
    )

    try:
        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "created"}
    finally:
        api_delete_resource(resource_id)


def test_17_post_resource_with_file_url_creates_file_resource():
    resource_id = RESOURCE_ID_BASE + 14
    upload = request_upload_url("api-test.txt", "text/plain")
    file_url = upload["public_url"]

    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "API test file resource",
            "content": file_url,
            "is_file": True,
        },
    )

    try:
        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "created"}

        list_response = requests.get(
            f"{BASE_URL}/api/resources",
            headers=auth_headers(get_session_token()),
        )
        assert list_response.status_code == 200

        resources = {item["id"]: item for item in list_response.json()["data"]}
        assert resources[resource_id] == {
            "id": resource_id,
            "title": "API test file resource",
            "content": file_url,
            "is_file": True,
        }
    finally:
        api_delete_resource(resource_id)


def test_18_get_text_resource_by_id_returns_single_resource():
    resource_id = RESOURCE_ID_BASE + 15

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Single text resource",
            "content": "Plain text body from pytest",
            "is_file": False,
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {
            "id": resource_id,
            "title": "Single text resource",
            "content": "Plain text body from pytest",
            "is_file": False,
        }
    finally:
        api_delete_resource(resource_id)


def test_19_get_file_url_resource_by_id_returns_single_resource():
    resource_id = RESOURCE_ID_BASE + 16
    upload = request_upload_url("single-api-test.pdf", "application/pdf")
    file_url = upload["public_url"]

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Single file resource",
            "content": file_url,
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {
            "id": resource_id,
            "title": "Single file resource",
            "content": file_url,
            "is_file": True,
        }
    finally:
        api_delete_resource(resource_id)


def test_20_put_resource_skipping_id_returns_error():
    response = requests.put(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={"title": "Missing id", "content": "This should fail"},
    )

    assert_json_error(response, 400, "invalid id")


def test_21_put_resource_with_no_token_or_wrong_token_is_forbidden():
    body = {
        "id": RESOURCE_ID_BASE + 15,
        "title": "Unauthorized update",
        "content": "This should fail",
    }

    no_token_response = requests.put(f"{BASE_URL}/api/resources", json=body)
    wrong_token_response = requests.put(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
        json=body,
    )

    assert no_token_response.status_code in (400, 403)
    assert wrong_token_response.status_code == 403


def test_22_put_resource_with_full_message_updates_resource():
    resource_id = RESOURCE_ID_BASE + 16

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Before update",
            "content": "Before content",
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.put(
            f"{BASE_URL}/api/resources",
            headers=auth_headers(get_session_token()),
            json={
                "id": resource_id,
                "title": "After update",
                "content": "After content",
            },
        )

        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "updated"}
    finally:
        api_delete_resource(resource_id)


def test_23_delete_resource_with_wrong_token_is_forbidden():
    response = api_delete_resource(RESOURCE_ID_BASE + 17, token=WRONG_TOKEN)

    assert response.status_code == 403


def test_24_delete_resource_with_correct_token_but_no_id_returns_error():
    response = requests.delete(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
    )

    assert_json_error(response, 400, "invalid parameter")


def test_25_delete_resource_with_full_message_deletes_resource():
    resource_id = RESOURCE_ID_BASE + 19

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Delete me",
            "content": "Created so delete can be tested",
        },
    )
    assert create_response.status_code == 200

    response = api_delete_resource(resource_id)

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"status": "deleted"}


def test_26_logout_with_valid_token_returns_success():
    login_response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": ANDREW_PASSWORD},
    )
    assert login_response.status_code == 200

    token = remember_token(login_response.json()["token"])
    response = requests.post(
        f"{BASE_URL}/api/logout",
        headers=auth_headers(token),
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"message": "logout success"}


def test_27_register_with_invalid_json_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        headers={"Content-Type": "application/json"},
        data="{bad json",
    )

    assert_json_error(response, 400, "invalid JSON format")


def test_28_register_skipping_id_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "name": "Missing Id",
            "email": "missing-id@test.com",
            "password": "test-password",
        },
    )

    assert_json_error(response, 400, "invalid id")


def test_29_register_with_non_integer_id_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "id": "not-an-int",
            "name": "Wrong Id",
            "email": "wrong-id@test.com",
            "password": "test-password",
        },
    )

    assert_json_error(response, 400, "invalid id")


def test_30_register_duplicate_existing_user_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "id": 1,
            "name": "Andrew",
            "email": ANDREW_EMAIL,
            "password": ANDREW_PASSWORD,
        },
    )

    assert_json_has_error(response, 400)


def test_31_register_skipping_email_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "id": RESOURCE_ID_BASE + 25,
            "name": "Missing Email",
            "password": "test-password",
        },
    )

    assert_json_error(response, 400, "missing fields")


def test_32_register_skipping_password_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "id": RESOURCE_ID_BASE + 26,
            "name": "Missing Password",
            "email": "missing-password@test.com",
        },
    )

    assert_json_error(response, 400, "missing fields")


def request_upload_url(filename, content_type):
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={"filename": filename, "content_type": content_type},
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    return response.json()


def test_33_presigned_upload_url_accepts_put_file_bytes():
    upload = request_upload_url("pytest-upload.txt", "text/plain")

    response = requests.put(
        upload["upload_url"],
        data=b"hello from pytest via presigned url",
        headers={"Content-Type": "text/plain"},
    )

    assert response.status_code in (200, 204)


def test_34_uploaded_file_url_can_be_saved_as_resource():
    resource_id = RESOURCE_ID_BASE + 34
    upload = request_upload_url("pytest-resource.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"hello from pytest resource flow",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Uploaded pytest file",
            "content": upload["public_url"],
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    try:
        get_response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert get_response.status_code == 200
        assert get_response.headers["content-type"].startswith("application/json")
        assert get_response.json() == {
            "id": resource_id,
            "title": "Uploaded pytest file",
            "content": upload["public_url"],
            "is_file": True,
        }
    finally:
        api_delete_resource(resource_id)


def test_35_download_url_requires_token():
    response = requests.get(f"{BASE_URL}/api/files/download-url?resource_id=1")

    assert response.status_code == 403


def test_36_download_url_missing_resource_id_returns_error():
    response = requests.get(
        f"{BASE_URL}/api/files/download-url",
        headers=auth_headers(get_session_token()),
    )

    assert_json_error(response, 400, "invalid parameter")


def test_37_download_url_for_text_resource_returns_error():
    resource_id = RESOURCE_ID_BASE + 37

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Text resource no download",
            "content": "plain text only",
            "is_file": False,
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert_json_error(response, 400, "resource is not file")
    finally:
        api_delete_resource(resource_id)


def test_38_download_url_for_file_resource_returns_presigned_url():
    resource_id = RESOURCE_ID_BASE + 38
    upload = request_upload_url("pytest-download-url.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"hello for download url",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Download URL pytest file",
            "content": upload["public_url"],
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")

        body = response.json()
        parsed = urlparse(body["download_url"])
        query = parse_qs(parsed.query)

        assert body["public_url"] == upload["public_url"]
        assert body["object_key"] == upload["object_key"]
        assert body["bucket"] == "webserver-files"
        assert body["expires_in"] == 300
        assert parsed.path == f"/webserver-files/{upload['object_key']}"
        assert query["X-Amz-Algorithm"] == ["AWS4-HMAC-SHA256"]
        assert query["X-Amz-Expires"] == ["300"]
        assert query["X-Amz-SignedHeaders"] == ["host"]
        assert "X-Amz-Signature" in query
    finally:
        api_delete_resource(resource_id)


def test_39_download_url_returns_uploaded_file_bytes():
    resource_id = RESOURCE_ID_BASE + 39
    file_bytes = b"download me through presigned get"
    upload = request_upload_url("pytest-download-bytes.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=file_bytes,
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Download bytes pytest file",
            "content": upload["public_url"],
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    try:
        download_url_response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_session_token()),
        )
        assert download_url_response.status_code == 200

        download_response = requests.get(download_url_response.json()["download_url"])

        assert download_response.status_code == 200
        assert download_response.content == file_bytes
    finally:
        api_delete_resource(resource_id)


def test_40_upload_url_rejects_path_traversal_filename():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={
            "filename": "../secret.txt",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "invalid filename")


def test_41_upload_url_rejects_blocked_extension():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={
            "filename": "run-me.sh",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "blocked file type")


def test_42_upload_url_rejects_unsupported_content_type():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={
            "filename": "payload.bin",
            "content_type": "application/octet-stream",
        },
    )

    assert_json_error(response, 400, "unsupported content_type")


def test_43_upload_url_rejects_filename_that_is_too_long():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_session_token()),
        json={
            "filename": f"{'a' * 256}.txt",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "filename too long")


def test_44_upload_url_uses_configured_expiry():
    upload = request_upload_url("pytest-expiry-upload.txt", "text/plain")
    query = parse_qs(urlparse(upload["upload_url"]).query)

    assert upload["expires_in"] == 300
    assert query["X-Amz-Expires"] == ["300"]


def test_45_download_url_uses_configured_expiry():
    resource_id = RESOURCE_ID_BASE + 45
    upload = request_upload_url("pytest-expiry-download.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"expiry config check",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Download expiry pytest file",
            "content": upload["public_url"],
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_session_token()),
        )

        assert response.status_code == 200
        body = response.json()
        query = parse_qs(urlparse(body["download_url"]).query)

        assert body["expires_in"] == 300
        assert query["X-Amz-Expires"] == ["300"]
    finally:
        api_delete_resource(resource_id)


def test_46_delete_file_resource_removes_minio_object():
    resource_id = RESOURCE_ID_BASE + 46
    file_bytes = b"this object should disappear after resource delete"
    upload = request_upload_url("pytest-delete-object.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=file_bytes,
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    create_response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_session_token()),
        json={
            "id": resource_id,
            "title": "Delete object pytest file",
            "content": upload["public_url"],
            "is_file": True,
        },
    )
    assert create_response.status_code == 200

    download_url_response = requests.get(
        f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
        headers=auth_headers(get_session_token()),
    )
    assert download_url_response.status_code == 200

    download_url = download_url_response.json()["download_url"]
    before_delete_response = requests.get(download_url)
    assert before_delete_response.status_code == 200
    assert before_delete_response.content == file_bytes

    delete_response = api_delete_resource(resource_id)
    assert delete_response.status_code == 200
    assert delete_response.json()["status"] == "deleted"

    after_delete_response = requests.get(download_url)
    assert after_delete_response.status_code in (403, 404)
