import Link from "next/link";

export default function Home() {
  return (
    <main className="flex min-h-screen flex-col items-center justify-center gap-8 bg-slate-50 px-6 text-center text-slate-950">
      <div className="space-y-3">
        <p className="text-sm font-medium uppercase tracking-[0.2em] text-emerald-700">
          C++ Auth Resource Server
        </p>
        <h1 className="text-4xl font-semibold">Resource Manager</h1>
        <p className="max-w-xl text-base leading-7 text-slate-600">
          Sign in or create an account to start managing authenticated resources.
        </p>
      </div>

      <nav className="flex flex-wrap justify-center gap-3">
        <Link className="rounded-md bg-slate-950 px-4 py-2 text-white" href="/login">
          Login
        </Link>
        <Link
          className="rounded-md border border-slate-300 px-4 py-2 text-slate-900"
          href="/register"
        >
          Register
        </Link>
      </nav>
    </main>
  );
}
