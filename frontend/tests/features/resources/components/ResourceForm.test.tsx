import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { ResourceForm } from "@/features/resources/components/ResourceForm";

describe("ResourceForm", () => {
  it("submits typed title and content", async () => {
    const user = userEvent.setup();
    const onSubmit = vi.fn();

    render(
      <ResourceForm
        isSubmitting={false}
        onSubmit={onSubmit}
        submitLabel="Create resource"
      />,
    );

    await user.type(screen.getByLabelText("Title"), "My note");
    await user.type(screen.getByLabelText("Content"), "Hello from a test");
    await user.click(screen.getByRole("button", { name: "Create resource" }));

    expect(onSubmit).toHaveBeenCalledWith({
      title: "My note",
      content: "Hello from a test",
    });
  });

  it("renders initial values for editing", () => {
    render(
      <ResourceForm
        initialValues={{ title: "Existing", content: "Saved body" }}
        isSubmitting={false}
        onSubmit={vi.fn()}
        submitLabel="Update resource"
      />,
    );

    expect(screen.getByLabelText("Title")).toHaveValue("Existing");
    expect(screen.getByLabelText("Content")).toHaveValue("Saved body");
  });

  it("can make only the content field fill and scroll within the available height", () => {
    const { container } = render(
      <ResourceForm
        fillAvailableHeight
        isSubmitting={false}
        onSubmit={vi.fn()}
        submitLabel="Create resource"
      />,
    );
    const form = container.querySelector("form");

    expect(form).toHaveClass("flex", "h-full", "min-h-0");
    expect(screen.getByLabelText("Content")).toHaveClass(
      "flex-1",
      "overflow-y-auto",
      "resize-none",
    );
  });
});
