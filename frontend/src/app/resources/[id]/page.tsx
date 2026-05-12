import { ResourceDetail } from "@/features/resources/components/ResourceDetail";

type ResourceDetailPageProps = {
  params: Promise<{
    id: string;
  }>;
};

export default async function ResourceDetailPage({
  params,
}: ResourceDetailPageProps) {
  // App Router page files translate URL segments into feature components.
  const { id } = await params;

  return <ResourceDetail id={Number(id)} />;
}
