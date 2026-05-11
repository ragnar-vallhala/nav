import Home from '../../../page';

export default async function SinglePostPage({ params }: any) {
  // Next.js dynamic parameters are now async promises in recent specifications
  const resolvedParams = await params;
  return <Home defaultContext="community" activePostId={resolvedParams.id} />;
}
