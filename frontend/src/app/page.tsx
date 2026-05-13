import Link from "next/link";

export default function Home() {
  return (
    <main className="relative min-h-screen overflow-hidden bg-[#202124] text-white">
      <div
        aria-hidden="true"
        className="home-page-background absolute inset-0 bg-[url('/home_page.jpg')] bg-cover bg-center"
      />
      <div aria-hidden="true" className="absolute inset-0 bg-black/65" />
      <section className="relative z-10 min-h-[74vh] px-6 py-8">
        <header className="relative z-10 mx-auto flex max-w-7xl items-center justify-between">
          <Link className="flex items-center gap-3 font-semibold" href="/">
            <span className="flex size-10 items-center justify-center rounded-md bg-white text-xl font-bold text-slate-950">
              R
            </span>
            <span className="text-xl">esource Manager</span>
          </Link>

          <nav className="hidden items-center gap-10 text-base font-bold text-white/85 md:flex">
            <Link
              className="text-white transition hover:-translate-y-0.5 hover:text-white/70"
              href="/resources"
            >
              Resources
            </Link>
            <Link
              className="transition hover:-translate-y-0.5 hover:text-white/70"
              href="/login"
            >
              Login
            </Link>
          </nav>
        </header>

        <div className="relative z-10 mx-auto flex max-w-7xl justify-end py-20 md:py-28">
          <div className="max-w-xl text-left md:text-center">
            <h1 className="home-fade-in text-3xl font-semibold leading-tight text-white md:text-5xl">
              A Reliable Way to Manage Resources
            </h1>
            <p className="home-fade-in home-fade-in-delay-1 mt-6 text-base leading-8 text-white/65 md:text-lg">
              Sign in or create an account to manage authenticated text and file
              resources through a clean web interface.
            </p>

            <div className="home-fade-in home-fade-in-delay-2 mt-9 flex flex-wrap gap-3 md:justify-center">
              <Link
                className="rounded-md bg-white px-7 py-4 text-base font-bold text-slate-950 shadow-lg shadow-black/25 transition hover:-translate-y-0.5 hover:bg-slate-200"
                href="/register"
              >
                Creat account
              </Link>
            </div>
          </div>
        </div>
      </section>

      <section className="relative z-10 px-6 pb-16 pt-0">
        <div className="mx-auto max-w-7xl">
          <div className="max-w-xl">
            <p className="text-sm font-semibold uppercase tracking-[0.2em] text-white/60">
              Start Exploring
            </p>
            <h2 className="mt-4 text-3xl font-semibold text-white">
              Keep resource work organized.
            </h2>
            <p className="mt-4 leading-7 text-white/65">
              Create resources, upload files, and return to your saved content
              from one focused dashboard.
            </p>
          </div>
        </div>
      </section>
    </main>
  );
}
