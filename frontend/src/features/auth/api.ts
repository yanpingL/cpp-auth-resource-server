import { apiFetch } from "@/shared/api/client";

// Auth feature API: keeps backend endpoint details out of UI components.
export type RegisterRequest = {
  name: string;
  email: string;
  password: string;
};

export type LoginRequest = {
  email: string;
  password: string;
};

export type RegisterResponse = {
  status: "created";
};

export type LoginResponse = {
  user_id: number;
  name: string;
  token: string;
};

export function registerUser(payload: RegisterRequest) {
  return apiFetch<RegisterResponse>("/api/register", {
    method: "POST",
    body: JSON.stringify(payload),
  });
}

export function loginUser(payload: LoginRequest) {
  return apiFetch<LoginResponse>("/api/login", {
    method: "POST",
    body: JSON.stringify(payload),
  });
}
