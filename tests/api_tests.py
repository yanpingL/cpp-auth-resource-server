import base64
import json
import os
import time
from urllib.parse import parse_qs, urlparse

import requests


BASE_URL = os.getenv("BASE_URL", "http://localhost:8080")
TEST_RUN_ID = str(int(time.time()))
ANDREW_EMAIL = os.getenv("ANDREW_EMAIL", "andrew@test.com")
ANDREW_PASSWORD = os.getenv("ANDREW_PASSWORD", "hash3")
JWT_ISSUER = os.getenv("JWT_ISSUER", "webserver")
WRONG_TOKEN = "0" * 64

ACCESS_TOKEN = os.getenv("API_TOKEN")
USER_ID = None


def auth_headers(token):
    return {"Authorization": f"Bearer {token}"}


def assert_json_error(response, status_code, message):
    assert response.status_code == status_code
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"error": message}


def assert_json_has_error(response, status_code):
    assert response.status_code == status_code
    assert response.headers["content-type"].startswith("application/json")
    assert "error" in response.json()


def decode_jwt_payload(token):
    parts = token.split(".")
    assert len(parts) == 3

    payload = parts[1]
    payload += "=" * (-len(payload) % 4)
    return json.loads(base64.urlsafe_b64decode(payload.encode("ascii")))


def assert_valid_jwt_shape(token, user_id=None):
    assert isinstance(token, str)
    assert token.count(".") == 2

    payload = decode_jwt_payload(token)
    assert payload["iss"] == JWT_ISSUER
    assert isinstance(payload["iat"], int)
    assert isinstance(payload["exp"], int)
    assert payload["exp"] > payload["iat"]
    assert payload["exp"] > int(time.time())

    if user_id is not None:
        assert payload["sub"] == str(user_id)


def register_test_user():
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "name": "Andrew",
            "email": ANDREW_EMAIL,
            "password": ANDREW_PASSWORD,
        },
    )

    if response.status_code == 200:
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "created"}
    else:
        # The user may already exist if the database volume was reused.
        assert_json_has_error(response, 400)


def login_test_user():
    global ACCESS_TOKEN, USER_ID

    if ACCESS_TOKEN is not None and USER_ID is not None:
        return ACCESS_TOKEN

    register_test_user()
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": ANDREW_PASSWORD},
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")

    body = response.json()
    assert isinstance(body["user_id"], int)
    assert_valid_jwt_shape(body["token"], body["user_id"])

    USER_ID = body["user_id"]
    ACCESS_TOKEN = body["token"]
    return ACCESS_TOKEN


def get_access_token():
    return login_test_user()


def list_resources(token=None):
    response = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(token or get_access_token()),
    )
    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    return response.json()["data"]


def find_resource_id(title, content=None, is_file=None):
    matches = []
    for item in list_resources():
        if item["title"] != title:
            continue
        if content is not None and item["content"] != content:
            continue
        if is_file is not None and item["is_file"] != is_file:
            continue
        matches.append(item)

    assert matches, f"resource not found for title={title!r}"
    return max(matches, key=lambda item: item["id"])["id"]


def create_resource(title, content="", is_file=False):
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
        json={
            "title": title,
            "content": content,
            "is_file": is_file,
        },
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"status": "created"}

    return find_resource_id(title, content, is_file)


def api_delete_resource(resource_id, token=None):
    return requests.delete(
        f"{BASE_URL}/api/resources?id={resource_id}",
        headers=auth_headers(token or get_access_token()),
    )


def request_upload_url(filename, content_type):
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={"filename": filename, "content_type": content_type},
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    return response.json()


def test_01_register_missing_fields_returns_error():
    response = requests.post(f"{BASE_URL}/api/register", json={})

    assert_json_error(response, 400, "missing fields")


def test_02_register_invalid_json_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/register",
        headers={"Content-Type": "application/json"},
        data="{bad json",
    )

    assert_json_error(response, 400, "invalid JSON format")


def test_03_register_valid_user_without_id_returns_created_or_duplicate():
    register_test_user()


def test_04_register_duplicate_existing_user_returns_error():
    register_test_user()
    response = requests.post(
        f"{BASE_URL}/api/register",
        json={
            "name": "Andrew",
            "email": ANDREW_EMAIL,
            "password": ANDREW_PASSWORD,
        },
    )

    assert_json_has_error(response, 400)


def test_05_login_missing_fields_returns_error():
    response = requests.post(f"{BASE_URL}/api/login", json={})

    assert_json_error(response, 400, "missing fields")


def test_06_login_only_with_email_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL},
    )

    assert_json_error(response, 400, "missing fields")


def test_07_login_wrong_email_correct_password_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": f"missing-{TEST_RUN_ID}@test.com", "password": ANDREW_PASSWORD},
    )

    assert_json_error(response, 400, "User not found")


def test_08_login_wrong_password_returns_error():
    register_test_user()
    response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": "wrong-password"},
    )

    assert_json_error(response, 400, "wrong password")


def test_09_login_valid_user_returns_token():
    token = login_test_user()

    assert isinstance(USER_ID, int)
    assert_valid_jwt_shape(token, USER_ID)


def test_10_upload_url_requires_token():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        json={"filename": "report.pdf", "content_type": "application/pdf"},
    )

    assert response.status_code == 403


def test_11_upload_url_missing_fields_returns_error():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={"filename": "report.pdf"},
    )

    assert_json_error(response, 400, "missing fields")


def test_12_upload_url_valid_request_returns_presigned_url():
    upload = request_upload_url("report.pdf", "application/pdf")

    assert upload["bucket"] == "webserver-files"
    assert upload["content_type"] == "application/pdf"
    assert upload["object_key"].startswith(f"users/{USER_ID}/uploads/")
    assert upload["object_key"].endswith("-report.pdf")
    assert upload["public_url"] == f"http://localhost:9000/webserver-files/{upload['object_key']}"

    parsed = urlparse(upload["upload_url"])
    query = parse_qs(parsed.query)

    assert parsed.scheme == "http"
    assert parsed.netloc == "localhost:9000"
    assert parsed.path == f"/webserver-files/{upload['object_key']}"
    assert query["X-Amz-Algorithm"] == ["AWS4-HMAC-SHA256"]
    assert query["X-Amz-Expires"] == ["300"]
    assert query["X-Amz-SignedHeaders"] == ["host"]
    assert "X-Amz-Credential" in query
    assert "X-Amz-Date" in query
    assert "X-Amz-Signature" in query


def test_13_get_resources_requires_token():
    response = requests.get(f"{BASE_URL}/api/resources")

    assert response.status_code == 403


def test_14_get_resources_rejects_wrong_token():
    response = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
    )

    assert response.status_code == 403


def test_15_get_resources_returns_data_array():
    body = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
    ).json()

    assert "data" in body
    assert isinstance(body["data"], list)


def test_16_post_resource_with_wrong_token_is_forbidden():
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
        json={"title": "Wrong token", "content": "This should fail"},
    )

    assert response.status_code == 403


def test_17_post_resource_missing_title_is_allowed_by_current_api():
    content = f"Created without title {TEST_RUN_ID}"
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
        json={"content": content},
    )

    resource_id = None
    try:
        assert response.status_code == 200
        assert response.json() == {"status": "created"}
        resource_id = find_resource_id("", content, False)
    finally:
        if resource_id is not None:
            api_delete_resource(resource_id)


def test_18_post_resource_missing_content_is_allowed_by_current_api():
    title = f"Created without content {TEST_RUN_ID}"
    response = requests.post(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
        json={"title": title},
    )

    resource_id = None
    try:
        assert response.status_code == 200
        assert response.json() == {"status": "created"}
        resource_id = find_resource_id(title, "", False)
    finally:
        if resource_id is not None:
            api_delete_resource(resource_id)


def test_19_post_resource_with_full_message_creates_resource():
    title = f"API test resource {TEST_RUN_ID}"
    content = "Created by pytest"
    resource_id = create_resource(title, content, False)

    try:
        response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_access_token()),
        )

        assert response.status_code == 200
        assert response.json() == {
            "id": resource_id,
            "title": title,
            "content": content,
            "is_file": False,
        }
    finally:
        api_delete_resource(resource_id)


def test_20_post_resource_with_file_url_creates_file_resource():
    title = f"API test file resource {TEST_RUN_ID}"
    upload = request_upload_url("api-test.txt", "text/plain")
    resource_id = create_resource(title, upload["public_url"], True)

    try:
        resources = {item["id"]: item for item in list_resources()}
        assert resources[resource_id] == {
            "id": resource_id,
            "title": title,
            "content": upload["public_url"],
            "is_file": True,
        }
    finally:
        api_delete_resource(resource_id)


def test_21_get_text_resource_by_id_returns_single_resource():
    title = f"Single text resource {TEST_RUN_ID}"
    content = "Plain text body from pytest"
    resource_id = create_resource(title, content, False)

    try:
        response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_access_token()),
        )

        assert response.status_code == 200
        assert response.json() == {
            "id": resource_id,
            "title": title,
            "content": content,
            "is_file": False,
        }
    finally:
        api_delete_resource(resource_id)


def test_22_put_resource_missing_id_returns_error():
    response = requests.put(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
        json={"title": "Missing id", "content": "This should fail"},
    )

    assert_json_error(response, 400, "invalid id")


def test_23_put_resource_with_wrong_token_is_forbidden():
    body = {
        "id": 999999,
        "title": "Unauthorized update",
        "content": "This should fail",
    }

    response = requests.put(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(WRONG_TOKEN),
        json=body,
    )

    assert response.status_code == 403


def test_24_put_resource_with_full_message_updates_resource():
    title = f"Before update {TEST_RUN_ID}"
    resource_id = create_resource(title, "Before content", False)

    try:
        response = requests.put(
            f"{BASE_URL}/api/resources",
            headers=auth_headers(get_access_token()),
            json={
                "id": resource_id,
                "title": f"After update {TEST_RUN_ID}",
                "content": "After content",
            },
        )

        assert response.status_code == 200
        assert response.headers["content-type"].startswith("application/json")
        assert response.json() == {"status": "updated"}
    finally:
        api_delete_resource(resource_id)


def test_25_delete_resource_with_wrong_token_is_forbidden():
    response = api_delete_resource(999999, token=WRONG_TOKEN)

    assert response.status_code == 403


def test_26_delete_resource_with_correct_token_but_no_id_returns_error():
    response = requests.delete(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(get_access_token()),
    )

    assert_json_error(response, 400, "invalid parameter")


def test_27_delete_resource_with_full_message_deletes_resource():
    resource_id = create_resource(
        f"Delete me {TEST_RUN_ID}",
        "Created so delete can be tested",
        False,
    )

    response = api_delete_resource(resource_id)

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"status": "deleted"}


def test_28_logout_with_valid_token_returns_success():
    register_test_user()
    login_response = requests.post(
        f"{BASE_URL}/api/login",
        json={"email": ANDREW_EMAIL, "password": ANDREW_PASSWORD},
    )
    assert login_response.status_code == 200

    token = login_response.json()["token"]
    assert_valid_jwt_shape(token)
    response = requests.post(
        f"{BASE_URL}/api/logout",
        headers=auth_headers(token),
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert response.json() == {"status": "logout success"}


def test_28b_logout_does_not_revoke_stateless_jwt():
    token = get_access_token()

    logout_response = requests.post(
        f"{BASE_URL}/api/logout",
        headers=auth_headers(token),
    )
    assert logout_response.status_code == 200

    response = requests.get(
        f"{BASE_URL}/api/resources",
        headers=auth_headers(token),
    )

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("application/json")
    assert "data" in response.json()


def test_29_presigned_upload_url_accepts_put_file_bytes():
    upload = request_upload_url("pytest-upload.txt", "text/plain")

    response = requests.put(
        upload["upload_url"],
        data=b"hello from pytest via presigned url",
        headers={"Content-Type": "text/plain"},
    )

    assert response.status_code in (200, 204)


def test_30_uploaded_file_url_can_be_saved_as_resource():
    title = f"Uploaded pytest file {TEST_RUN_ID}"
    upload = request_upload_url("pytest-resource.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"hello from pytest resource flow",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    resource_id = create_resource(title, upload["public_url"], True)

    try:
        response = requests.get(
            f"{BASE_URL}/api/resources?id={resource_id}",
            headers=auth_headers(get_access_token()),
        )

        assert response.status_code == 200
        assert response.json() == {
            "id": resource_id,
            "title": title,
            "content": upload["public_url"],
            "is_file": True,
        }
    finally:
        api_delete_resource(resource_id)


def test_31_download_url_requires_token():
    response = requests.get(f"{BASE_URL}/api/files/download-url?resource_id=1")

    assert response.status_code == 403


def test_32_download_url_missing_resource_id_returns_error():
    response = requests.get(
        f"{BASE_URL}/api/files/download-url",
        headers=auth_headers(get_access_token()),
    )

    assert_json_error(response, 400, "invalid parameter")


def test_33_download_url_for_text_resource_returns_error():
    resource_id = create_resource(
        f"Text resource no download {TEST_RUN_ID}",
        "plain text only",
        False,
    )

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_access_token()),
        )

        assert_json_error(response, 400, "resource is not file")
    finally:
        api_delete_resource(resource_id)


def test_34_download_url_for_file_resource_returns_presigned_url():
    title = f"Download URL pytest file {TEST_RUN_ID}"
    upload = request_upload_url("pytest-download-url.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"hello for download url",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    resource_id = create_resource(title, upload["public_url"], True)

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_access_token()),
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


def test_35_download_url_returns_uploaded_file_bytes():
    title = f"Download bytes pytest file {TEST_RUN_ID}"
    file_bytes = b"download me through presigned get"
    upload = request_upload_url("pytest-download-bytes.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=file_bytes,
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    resource_id = create_resource(title, upload["public_url"], True)

    try:
        download_url_response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_access_token()),
        )
        assert download_url_response.status_code == 200

        download_response = requests.get(download_url_response.json()["download_url"])

        assert download_response.status_code == 200
        assert download_response.content == file_bytes
    finally:
        api_delete_resource(resource_id)


def test_36_upload_url_rejects_path_traversal_filename():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={
            "filename": "../secret.txt",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "invalid filename")


def test_37_upload_url_rejects_blocked_extension():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={
            "filename": "run-me.sh",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "blocked file type")


def test_38_upload_url_rejects_unsupported_content_type():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={
            "filename": "payload.bin",
            "content_type": "application/octet-stream",
        },
    )

    assert_json_error(response, 400, "unsupported content_type")


def test_39_upload_url_rejects_filename_that_is_too_long():
    response = requests.post(
        f"{BASE_URL}/api/files/upload-url",
        headers=auth_headers(get_access_token()),
        json={
            "filename": f"{'a' * 256}.txt",
            "content_type": "text/plain",
        },
    )

    assert_json_error(response, 400, "filename too long")


def test_40_upload_url_uses_configured_expiry():
    upload = request_upload_url("pytest-expiry-upload.txt", "text/plain")
    query = parse_qs(urlparse(upload["upload_url"]).query)

    assert upload["expires_in"] == 300
    assert query["X-Amz-Expires"] == ["300"]


def test_41_download_url_uses_configured_expiry():
    title = f"Download expiry pytest file {TEST_RUN_ID}"
    upload = request_upload_url("pytest-expiry-download.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=b"expiry config check",
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    resource_id = create_resource(title, upload["public_url"], True)

    try:
        response = requests.get(
            f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
            headers=auth_headers(get_access_token()),
        )

        assert response.status_code == 200
        body = response.json()
        query = parse_qs(urlparse(body["download_url"]).query)

        assert body["expires_in"] == 300
        assert query["X-Amz-Expires"] == ["300"]
    finally:
        api_delete_resource(resource_id)


def test_42_delete_file_resource_removes_minio_object():
    title = f"Delete object pytest file {TEST_RUN_ID}"
    file_bytes = b"this object should disappear after resource delete"
    upload = request_upload_url("pytest-delete-object.txt", "text/plain")

    upload_response = requests.put(
        upload["upload_url"],
        data=file_bytes,
        headers={"Content-Type": "text/plain"},
    )
    assert upload_response.status_code in (200, 204)

    resource_id = create_resource(title, upload["public_url"], True)

    download_url_response = requests.get(
        f"{BASE_URL}/api/files/download-url?resource_id={resource_id}",
        headers=auth_headers(get_access_token()),
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
