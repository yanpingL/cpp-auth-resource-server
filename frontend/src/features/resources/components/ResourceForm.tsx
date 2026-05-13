"use client";

import type { FormEvent } from "react";
import { useState } from "react";
import type { ResourceFormValues } from "../types";

type ResourceFormProps = {
  editorSize?: "default" | "large";
  initialValues?: ResourceFormValues;
  isSubmitting: boolean;
  onCancel?: () => void;
  onSubmit: (values: ResourceFormValues) => void;
  submitLabel: string;
};

const emptyValues: ResourceFormValues = {
  title: "",
  content: "",
};

export function ResourceForm({
  editorSize = "default",
  initialValues = emptyValues,
  isSubmitting,
  onCancel,
  onSubmit,
  submitLabel,
}: ResourceFormProps) {
  const [title, setTitle] = useState(initialValues.title);
  const [content, setContent] = useState(initialValues.content);

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    onSubmit({ title, content });
  }

  return (
    <form className="space-y-4" onSubmit={handleSubmit}>
      <label className="block space-y-2 text-sm font-medium">
        <span>Title</span>
        <input
          className="w-full rounded-md border border-slate-300 bg-slate-50 px-3 py-2 outline-none focus:border-slate-950"
          onChange={(event) => setTitle(event.target.value)}
          required
          value={title}
        />
      </label>

      <label className="block space-y-2 text-sm font-medium">
        <span>Content</span>
        <textarea
          className={`w-full resize-y rounded-md border border-slate-300 bg-slate-50 px-3 py-2 outline-none focus:border-slate-950 ${
            editorSize === "large" ? "min-h-[420px]" : "min-h-32"
          }`}
          onChange={(event) => setContent(event.target.value)}
          required
          value={content}
        />
      </label>

      <div className="flex flex-wrap gap-3">
        <button
          className="rounded-md bg-slate-950 px-4 py-2 text-sm font-semibold text-white disabled:opacity-50"
          disabled={isSubmitting}
          type="submit"
        >
          {isSubmitting ? "Saving..." : submitLabel}
        </button>
        {onCancel ? (
          <button
            className="rounded-md border border-slate-300 px-4 py-2 text-sm font-semibold"
            onClick={onCancel}
            type="button"
          >
            Cancel
          </button>
        ) : null}
      </div>
    </form>
  );
}
