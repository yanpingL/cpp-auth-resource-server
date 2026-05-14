# Resource Manager Frontend

This is the Next.js frontend for the C++ resource server.

## Getting Started

First, set the backend URL used by the Next.js API rewrite:

```bash
cp .env.example .env.local
```

For local development, `API_BASE_URL=http://localhost:8080` points the frontend
proxy at the local C++ backend or Nginx entrypoint.

Then run the development server:

```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser.

## Backend API Proxy

The frontend calls backend endpoints with relative URLs such as:

```ts
"/api/resources"
```

Next.js rewrites those requests to:

```ts
`${process.env.API_BASE_URL}/api/:path*`
```

That keeps browser traffic on the frontend origin while the Next.js server
proxies resource/auth API requests to the C++ backend.

For Vercel, set `API_BASE_URL` to the public backend URL, for example:

```bash
API_BASE_URL=https://api.example.com
```

Do not include a trailing `/api`; the rewrite appends `/api/:path*`.

## Resource API Stage

For this deployment stage, resource-related frontend calls stay behind the
Next.js rewrite:

- `GET /api/resources`
- `GET /api/resources?id=:id`
- `POST /api/resources`
- `PUT /api/resources`
- `DELETE /api/resources?id=:id`

File storage uses direct browser requests to signed storage URLs:

- `PUT` to the signed upload URL.
- `GET` or `HEAD` from signed preview/download URLs.

Because those requests do not pass through the Next.js rewrite, the storage
service needs its own CORS policy. Local MinIO allows the Next.js dev origin
through `MINIO_API_CORS_ALLOW_ORIGIN` in `../docker-compose.yml`.
`../deploy/minio/cors.xml` is a production bucket CORS template for
S3-compatible storage that supports bucket-level CORS configuration.

## Deploy on Vercel

This repository is a monorepo-style project, so the Vercel project should use
`frontend` as its root directory.

Recommended Vercel project settings:

```text
Root Directory: frontend
Framework Preset: Next.js
Install Command: npm install
Build Command: npm run build
Output Directory: .next
```

Set `API_BASE_URL` in the Vercel project environment variables before deploying
the frontend. Use the public backend origin only, without `/api`:

```bash
API_BASE_URL=https://api.example.com
```

Apply it to the Vercel environments you plan to use:

- Production
- Preview
- Development, if using `vercel dev`

Environment variable changes apply only to new deployments, so redeploy after
changing `API_BASE_URL`.

Build locally with:

```bash
npm run build
```

Deploy through the dashboard by importing the Git repository and selecting
`frontend` as the root directory.

Alternatively, with the Vercel CLI from the repository root:

```bash
vercel --cwd frontend
vercel --cwd frontend --prod
```

Backend production variables are listed in `../.env.production.example`. Those
belong to the backend container environment, not Vercel.
