export type Resource = {
  id: number;
  title: string;
  content: string;
  is_file: boolean;
};

export type ResourceFormValues = {
  title: string;
  content: string;
};
