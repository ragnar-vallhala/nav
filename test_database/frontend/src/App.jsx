import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  Activity,
  Archive,
  Boxes,
  CheckCircle2,
  ChevronRight,
  CircuitBoard,
  Copy,
  Database,
  Download,
  Github,
  Globe2,
  KeyRound,
  LockKeyhole,
  Mail,
  Monitor,
  Package,
  RefreshCcw,
  Search,
  ShieldCheck,
  UploadCloud,
  UserPlus,
  Wrench,
  X
} from 'lucide-react';
import './styles.css';

const API = import.meta.env.VITE_API_BASE || 'http://localhost:14000';
const TOKEN_KEY = 'nav_token';

function authHeaders(token) {
  return token ? { Authorization: `Bearer ${token}` } : {};
}

function cn(...classes) {
  return classes.filter(Boolean).join(' ');
}

function Button({ children, variant = 'default', size = 'default', className = '', ...props }) {
  return (
    <button className={cn('btn', `btn-${variant}`, `btn-${size}`, className)} {...props}>
      {children}
    </button>
  );
}

function Card({ children, className = '' }) {
  return <section className={cn('card', className)}>{children}</section>;
}

function Badge({ children, variant = 'secondary' }) {
  return <span className={cn('badge', `badge-${variant}`)}>{children}</span>;
}

function Input(props) {
  return <input className="input" {...props} />;
}

function Textarea(props) {
  return <textarea className="input textarea" {...props} />;
}

function Label({ children, className = '' }) {
  return <label className={cn('label', className)}>{children}</label>;
}

function Alert({ children, variant = 'default' }) {
  return <div className={cn('alert', `alert-${variant}`)}>{children}</div>;
}

function CommandLine({ command }) {
  const [copied, setCopied] = useState(false);

  async function copyCommand() {
    await navigator.clipboard.writeText(command);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1600);
  }

  return (
    <div className="command-line">
      <code>{command}</code>
      <Button
        className="copy-button"
        variant="ghost"
        size="sm"
        type="button"
        title={copied ? 'Copied' : 'Copy command'}
        aria-label={copied ? 'Command copied' : 'Copy command'}
        onClick={copyCommand}
      >
        {copied ? <CheckCircle2 size={16} /> : <Copy size={16} />}
      </Button>
    </div>
  );
}

function Table({ columns, rows, renderRow, empty }) {
  return (
    <div className="table-shell">
      <div className="table-row table-head" style={{ gridTemplateColumns: columns.grid }}>
        {columns.labels.map(label => <span key={label}>{label}</span>)}
      </div>
      {rows.length === 0 ? (
        <div className="empty-state">{empty}</div>
      ) : rows.map(row => (
        <div className="table-row" style={{ gridTemplateColumns: columns.grid }} key={row.key}>
          {renderRow(row)}
        </div>
      ))}
    </div>
  );
}

function App() {
  const [token, setToken] = useState(() => localStorage.getItem(TOKEN_KEY) || '');
  const [user, setUser] = useState(null);
  const [providers, setProviders] = useState({ email_password: true, google: false, github: false });
  const [stats, setStats] = useState(null);
  const [packages, setPackages] = useState([]);
  const [toolchains, setToolchains] = useState([]);
  const [namespaces, setNamespaces] = useState([]);
  const [members, setMembers] = useState([]);
  const [auditLogs, setAuditLogs] = useState([]);
  const [emailStatus, setEmailStatus] = useState(null);
  const [route, setRoute] = useState(() => normalizeRoute(window.location.pathname));
  const [query, setQuery] = useState('');
  const [activeVendor, setActiveVendor] = useState('all');
  const [toolchainFilters, setToolchainFilters] = useState({ os: 'all', arch: 'all', board: 'all' });
  const [showToolchainFilters, setShowToolchainFilters] = useState(false);
  const [form, setForm] = useState({ name: '', email: '', password: '' });
  const [otpForm, setOtpForm] = useState({ email: '', otp: '' });
  const [resetForm, setResetForm] = useState({ email: '', otp: '', password: '' });
  const [namespaceForm, setNamespaceForm] = useState({ name: '' });
  const [memberForm, setMemberForm] = useState({ namespace: 'nav', email: '', role: 'maintainer' });
  const [packageForm, setPackageForm] = useState({ namespace: 'nav', slug: '', description: '' });
  const [publishForm, setPublishForm] = useState({ namespace: 'nav', package: '', version: '0.1.0', file: null, changelogFile: null, changelogText: '' });
  const [toolchainPublishForm, setToolchainPublishForm] = useState({
    vendor_kind: 'nav',
    vendor_name: 'Nav',
    vendor_slug: 'nav',
    name: '',
    version: '',
    os: 'windows',
    arch: 'x64',
    description: '',
    homepage_url: '',
    upstream_url: '',
    signature: '',
    file: null
  });
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [loading, setLoading] = useState(false);
  const [theme, setTheme] = useState(() => localStorage.getItem('nav_theme') || 'light');

  const isAuthed = Boolean(token);

  useEffect(() => {
    document.documentElement.classList.toggle('dark', theme === 'dark');
    localStorage.setItem('nav_theme', theme);
  }, [theme]);

  useEffect(() => {
    const url = new URL(window.location.href);
    const oauthToken = url.searchParams.get('token');
    if (oauthToken) {
      localStorage.setItem(TOKEN_KEY, oauthToken);
      setToken(oauthToken);
      url.searchParams.delete('token');
      window.history.replaceState({}, '', '/');
      setRoute('home');
      setNotice('OAuth login completed.');
    }
  }, []);

  useEffect(() => {
    const onPop = () => setRoute(normalizeRoute(window.location.pathname));
    window.addEventListener('popstate', onPop);
    return () => window.removeEventListener('popstate', onPop);
  }, []);

  async function request(path, options = {}) {
    const response = await fetch(`${API}${path}`, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...authHeaders(token),
        ...(options.headers || {})
      }
    });
    const json = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(json.error || `Request failed: ${response.status}`);
      error.body = json;
      throw error;
    }
    return json;
  }

  async function load() {
    setLoading(true);
    setError('');
    try {
      const [providerData, statsData, packageData, toolchainData] = await Promise.all([
        request('/auth/providers'),
        request('/stats'),
        request('/packages'),
        request('/toolchains')
      ]);
      setProviders(providerData);
      setStats(statsData);
      setPackages(packageData.packages || []);
      setToolchains(toolchainData.toolchains || []);
      if (token) {
        const [me, namespaceData, auditData, emailData] = await Promise.all([
          request('/me').catch(() => null),
          request('/namespaces').catch(() => ({ namespaces: [] })),
          request('/audit').catch(() => ({ audit_logs: [] })),
          request('/email/status').catch(() => null)
        ]);
        setUser(me?.user || null);
        setNamespaces(namespaceData.namespaces || []);
        setAuditLogs(auditData.audit_logs || []);
        setEmailStatus(emailData);
        const selectedNamespace = route.startsWith('namespace-settings:')
          ? routeParams(route).namespace
          : memberForm.namespace || namespaceData.namespaces?.[0]?.name;
        if (selectedNamespace) {
          const memberData = await request(`/namespaces/${selectedNamespace}/members`).catch(() => ({ members: [] }));
          setMembers(memberData.members || []);
        }
      } else {
        setNamespaces([]);
        setMembers([]);
        setAuditLogs([]);
        setEmailStatus(null);
      }
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }

  async function loadNamespaceMembers(namespace) {
    if (!token || !namespace) {
      setMembers([]);
      return;
    }
    const memberData = await request(`/namespaces/${namespace}/members`).catch(() => ({ members: [] }));
    setMembers(memberData.members || []);
  }

  useEffect(() => {
    load();
  }, [token]);

  useEffect(() => {
    if (route.startsWith('namespace-settings:')) {
      loadNamespaceMembers(routeParams(route).namespace);
    }
  }, [route, token]);

  function navigate(nextRoute) {
    const path = routeToPath(nextRoute);
    window.history.pushState({}, '', path);
    setRoute(nextRoute);
    setError('');
    setNotice('');
  }

  async function submitAuth(event, mode) {
    event.preventDefault();
    setError('');
    setNotice('');
    const endpoint = mode === 'login' ? '/auth/login' : '/auth/register';
    try {
      const data = await request(endpoint, {
        method: 'POST',
        body: JSON.stringify(form)
      });
      if (data.pending_verification) {
        setOtpForm({ email: data.email || form.email, otp: '' });
        setNotice(data.mail?.sent ? 'Verification code sent. It expires in 5 minutes.' : 'Account created. Configure SMTP or resend the verification code.');
        navigate('verify-email');
        return;
      }
      localStorage.setItem(TOKEN_KEY, data.token);
      setToken(data.token);
      setUser(data.user);
      setNotice('Authenticated.');
      navigate('home');
    } catch (err) {
      if (err.body?.pending_verification) {
        setOtpForm({ email: err.body.email || form.email, otp: '' });
        setNotice('Email verification required. A new code was sent if email is configured.');
        navigate('verify-email');
        return;
      }
      setError(err.message);
    }
  }

  async function verifyEmail(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request('/auth/verify-email', {
        method: 'POST',
        body: JSON.stringify(otpForm)
      });
      localStorage.setItem(TOKEN_KEY, data.token);
      setToken(data.token);
      setUser(data.user);
      setNotice('Email verified.');
      navigate('home');
    } catch (err) {
      setError(err.message);
    }
  }

  async function requestPasswordReset(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      await request('/auth/password/forgot', {
        method: 'POST',
        body: JSON.stringify({ email: resetForm.email })
      });
      setNotice('If the account exists, a reset code was sent. It expires in 5 minutes.');
      navigate('reset-password');
    } catch (err) {
      setError(err.message);
    }
  }

  async function resetPassword(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request('/auth/password/reset', {
        method: 'POST',
        body: JSON.stringify(resetForm)
      });
      localStorage.setItem(TOKEN_KEY, data.token);
      setToken(data.token);
      setUser(data.user);
      setNotice('Password reset complete.');
      navigate('home');
    } catch (err) {
      setError(err.message);
    }
  }

  function logout() {
    localStorage.removeItem(TOKEN_KEY);
    setToken('');
    setUser(null);
    navigate('login');
  }

  async function createNamespace(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request('/namespaces', {
        method: 'POST',
        body: JSON.stringify(namespaceForm)
      });
      setNotice(`Namespace ${data.namespace.name} created.`);
      setMemberForm({ ...memberForm, namespace: data.namespace.name });
      setPackageForm({ ...packageForm, namespace: data.namespace.name });
      setPublishForm({ ...publishForm, namespace: data.namespace.name });
      await load();
    } catch (err) {
      setError(err.message);
    }
  }

  async function addNamespaceMember(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request(`/namespaces/${memberForm.namespace}/members`, {
        method: 'POST',
        body: JSON.stringify({ email: memberForm.email, role: memberForm.role })
      });
      setNotice(data.mail?.sent
        ? `Added ${data.member.email} as ${data.member.role} and sent an invite email.`
        : `Added ${data.member.email} as ${data.member.role}. Mail was not sent: ${data.mail?.reason || data.mail?.error || 'unknown reason'}.`);
      await load();
      await loadNamespaceMembers(memberForm.namespace);
    } catch (err) {
      setError(err.message);
    }
  }

  async function createPackage(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request('/packages', {
        method: 'POST',
        body: JSON.stringify({
          namespace: packageForm.namespace,
          slug: packageForm.slug,
          name: packageForm.slug,
          description: packageForm.description,
          readme: `# ${packageForm.slug}\n\n${packageForm.description}`
        })
      });
      setNotice(`Package ${data.package.namespace}/${data.package.slug} created.`);
      setPublishForm({ ...publishForm, namespace: data.package.namespace, package: data.package.slug });
      await load();
    } catch (err) {
      setError(err.message);
    }
  }

  async function publishVersion(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    if (!publishForm.file) {
      setError('Choose a package archive file first.');
      return;
    }
    try {
      const bytes = await publishForm.file.arrayBuffer();
      const changelogFromFile = publishForm.changelogFile ? await publishForm.changelogFile.text() : '';
      const changelog = (changelogFromFile || publishForm.changelogText || '').trim();
      const expected_sha256 = await sha256Hex(bytes);
      const init = await request('/publish/init', {
        method: 'POST',
        body: JSON.stringify({
          namespace: publishForm.namespace,
          package: publishForm.package,
          version: publishForm.version,
          file_name: publishForm.file.name,
          content_type: publishForm.file.type || 'application/octet-stream',
          size_bytes: publishForm.file.size,
          expected_sha256
        })
      });
      const uploadResponse = await fetch(init.upload.url, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/octet-stream',
          ...authHeaders(token)
        },
        body: bytes
      });
      if (!uploadResponse.ok) {
        const body = await uploadResponse.json().catch(() => ({}));
        throw new Error(body.error || `Upload failed: ${uploadResponse.status}`);
      }
      await request(`/publish/${init.session.id}/complete`, {
        method: 'POST',
        body: JSON.stringify({
          manifest: {
            namespace: publishForm.namespace,
            name: publishForm.package,
            version: publishForm.version,
            source: 'dashboard',
            ...(changelog ? { changelog } : {})
          },
          readme: `# ${publishForm.package}\n\nPublished through Nav Registry.`,
          changelog
        })
      });
      setNotice(`Published ${publishForm.namespace}/${publishForm.package}@${publishForm.version}.`);
      await load();
    } catch (err) {
      setError(err.message);
    }
  }

  async function publishToolchain(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    if (!toolchainPublishForm.file) {
      setError('Choose a toolchain archive first.');
      return;
    }
    try {
      const bytes = await toolchainPublishForm.file.arrayBuffer();
      const metadata = {
        ...toolchainPublishForm,
        file: undefined,
        archive_format: toolchainPublishForm.file.name.toLowerCase().endsWith('.zip') ? 'zip' : 'tar.gz',
        content_type: toolchainPublishForm.file.type || 'application/octet-stream'
      };
      const response = await fetch(`${API}/toolchains/publish`, {
        method: 'POST',
        headers: {
          ...authHeaders(token),
          'Content-Type': 'application/octet-stream',
          'X-Nav-Toolchain-Metadata': JSON.stringify(metadata)
        },
        body: bytes
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(body.error || `Upload failed: ${response.status}`);
      setNotice(`Published managed toolchain ${toolchainPublishForm.name}@${toolchainPublishForm.version}.`);
      await load();
    } catch (err) {
      setError(err.message);
    }
  }

  async function downloadPackage(pkg) {
    const version = pkg.latest_version;
    if (!version) return;
    window.location.href = `${API}/packages/${pkg.namespace}/${pkg.slug}/versions/${version}/download`;
  }

  const filteredPackages = useMemo(() => {
    const value = query.trim().toLowerCase();
    if (!value) return packages;
    return packages.filter(pkg => {
      const haystack = `${pkg.namespace}/${pkg.slug} ${pkg.description} ${pkg.latest_version}`.toLowerCase();
      return haystack.includes(value);
    });
  }, [packages, query]);

  const toolchainGroups = useMemo(() => groupToolchains(toolchains), [toolchains]);

  const toolchainFilterOptions = useMemo(() => {
    const platforms = toolchains.map(toolchain => `${toolchain.os}/${toolchain.arch}`).filter(Boolean);
    const boards = new Set();
    toolchains.forEach(toolchain => getToolchainBoards(toolchain).forEach(board => boards.add(board)));
    return {
      os: [...new Set(toolchains.map(toolchain => toolchain.os).filter(Boolean))].sort(),
      arch: [...new Set(toolchains.map(toolchain => toolchain.arch).filter(Boolean))].sort(),
      boards: [...boards].sort(),
      platforms: [...new Set(platforms)].sort()
    };
  }, [toolchains]);

  const filteredToolchainGroups = useMemo(() => {
    return toolchainGroups.filter(group => {
      if (activeVendor !== 'all' && group.vendor_kind !== activeVendor) return false;
      if (toolchainFilters.os !== 'all' && !group.artifacts.some(artifact => artifact.os === toolchainFilters.os)) return false;
      if (toolchainFilters.arch !== 'all' && !group.artifacts.some(artifact => artifact.arch === toolchainFilters.arch)) return false;
      if (toolchainFilters.board !== 'all' && !group.boards.includes(toolchainFilters.board)) return false;
      return true;
    });
  }, [toolchainGroups, activeVendor, toolchainFilters]);

  const statCards = useMemo(() => [
    { label: 'Packages', value: stats?.packages ?? 0, icon: Package, detail: 'Public install targets' },
    { label: 'Versions', value: stats?.package_versions ?? 0, icon: Boxes, detail: 'Immutable releases' },
    { label: 'Package blobs', value: stats?.package_artifacts ?? 0, icon: Database, detail: 'MinIO archives' },
    { label: 'Toolchains', value: stats?.toolchains ?? 0, icon: Wrench, detail: 'Managed build tools' },
    { label: 'Namespaces', value: stats?.namespaces ?? 0, icon: Globe2, detail: 'Owner scopes' }
  ], [stats]);

  const sharedPageProps = {
    request,
    error,
    notice,
    stats,
    statCards,
    loading,
    load,
    query,
    setQuery,
    filteredPackages,
    downloadPackage,
    isAuthed,
    navigate,
    namespaces,
    members,
    auditLogs,
    namespaceForm,
    setNamespaceForm,
    memberForm,
    setMemberForm,
    packageForm,
    setPackageForm,
    publishForm,
    setPublishForm,
    toolchainPublishForm,
    setToolchainPublishForm,
    createNamespace,
    addNamespaceMember,
    createPackage,
    publishVersion,
    publishToolchain,
    activeVendor,
    setActiveVendor,
    filteredToolchainGroups,
    toolchainFilters,
    setToolchainFilters,
    toolchainFilterOptions,
    showToolchainFilters,
    setShowToolchainFilters,
    emailStatus
  };

  if (route === 'login' || route === 'signup') {
    return (
      <AuthPage
        mode={route}
        form={form}
        setForm={setForm}
        providers={providers}
        error={error}
        notice={notice}
        submitAuth={submitAuth}
        navigate={navigate}
      />
    );
  }

  if (route === 'verify-email') {
    return <VerifyEmailPage otpForm={otpForm} setOtpForm={setOtpForm} error={error} notice={notice} verifyEmail={verifyEmail} navigate={navigate} />;
  }

  if (route === 'forgot-password' || route === 'reset-password') {
    return (
      <PasswordResetPage
        mode={route}
        resetForm={resetForm}
        setResetForm={setResetForm}
        error={error}
        notice={notice}
        requestPasswordReset={requestPasswordReset}
        resetPassword={resetPassword}
        navigate={navigate}
      />
    );
  }

  if (route === 'invite') {
    return <InvitePage navigate={navigate} isAuthed={isAuthed} />;
  }

  return (
    <main className="app">
      <Header
        route={route}
        navigate={navigate}
        isAuthed={isAuthed}
        user={user}
        logout={logout}
        theme={theme}
        setTheme={setTheme}
      />
      {route === 'packages' && <PackagesPage {...sharedPageProps} />}
      {route.startsWith('package-detail:') && <PackageDetailPage {...sharedPageProps} route={route} />}
      {route.startsWith('package-upload:') && <PackageUploadPage {...sharedPageProps} route={route} />}
      {route === 'namespaces' && <NamespacesPage {...sharedPageProps} />}
      {route.startsWith('namespace-settings:') && <NamespaceSettingsPage {...sharedPageProps} route={route} />}
      {route === 'toolchains' && <ToolchainsPage {...sharedPageProps} />}
      {route === 'settings' && isAuthed && <AccountSettingsPage {...sharedPageProps} user={user} />}
      {route === 'admin' && user?.system_role === 'root' && <AdminPage {...sharedPageProps} />}
      {route === 'admin' && user?.system_role !== 'root' && <Page><div className="empty-state">Root administrator access is required.</div></Page>}
      {route === 'home' && <HomePage />}
    </main>
  );
}

function Header({ route, navigate, isAuthed, user, logout, theme, setTheme }) {
  const links = [
    ['home', 'Registry'],
    ['packages', 'Packages'],
    ['toolchains', 'Toolchains']
  ];
  if (isAuthed) links.push(['namespaces', 'Namespaces'], ['settings', 'Settings']);
  if (user?.system_role === 'root') links.push(['admin', 'Admin']);

  return (
    <header className="site-header">
      <button className="brand-button" onClick={() => navigate('home')}>
        <span className="brand-mark">N</span>
        <span>
          <strong>Nav</strong>
        </span>
      </button>
      <nav className="main-nav">
        {links.map(([key, label]) => (
          <button key={key} className={routeKey(route) === key ? 'active' : ''} onClick={() => navigate(key)}>
            {label}
          </button>
        ))}
      </nav>
      <div className="header-actions">
        <Button variant="outline" size="sm" onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>
          {theme === 'dark' ? 'Light' : 'Dark'}
        </Button>
        {isAuthed ? (
          <>
            <div className="user-pill">
              <KeyRound size={15} />
              <span>{user?.name || 'Signed in'}</span>
            </div>
            <Button variant="outline" size="sm" onClick={logout}>Sign out</Button>
          </>
        ) : (
          <>
            <Button variant="ghost" size="sm" onClick={() => navigate('login')}>Log in</Button>
            <Button size="sm" onClick={() => navigate('signup')}>Sign up</Button>
          </>
        )}
      </div>
    </header>
  );
}

function AuthPage({ mode, form, setForm, providers, error, notice, submitAuth, navigate }) {
  const isSignup = mode === 'signup';

  return (
    <main className="auth-page">
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <span className="brand-mark">N</span>
          <span><strong>Nav</strong></span>
        </button>
        <div className="auth-card-head">
          <h2>{isSignup ? 'Create account' : 'Log in'}</h2>
          {isSignup ? <UserPlus size={21} /> : <LockKeyhole size={21} />}
        </div>

        {error && <Alert variant="destructive">{error}</Alert>}
        {notice && <Alert variant="success">{notice}</Alert>}

        <form className="auth-form" onSubmit={event => submitAuth(event, isSignup ? 'register' : 'login')}>
          {isSignup && (
            <Label>
              Name
              <Input value={form.name} onChange={event => setForm({ ...form, name: event.target.value })} />
            </Label>
          )}
          <Label>
            Email
            <Input type="email" value={form.email} onChange={event => setForm({ ...form, email: event.target.value })} />
          </Label>
          <Label>
            Password
            <Input type="password" value={form.password} onChange={event => setForm({ ...form, password: event.target.value })} />
          </Label>
          <Button type="submit" className="full-button">
            {isSignup ? 'Create account' : 'Log in'}
            <ChevronRight size={16} />
          </Button>
        </form>

        <div className="divider"><span>or continue with</span></div>

        <div className="oauth-row">
          <a className={cn('btn btn-outline btn-default', !providers.google && 'disabled')} href={providers.google ? `${API}/auth/google` : undefined}>
            <Mail size={16} />
            Google
          </a>
          <a className={cn('btn btn-outline btn-default', !providers.github && 'disabled')} href={providers.github ? `${API}/auth/github` : undefined}>
            <Github size={16} />
            GitHub
          </a>
        </div>

        <p className="auth-switch">
          {isSignup ? 'Already have an account?' : 'New to Nav?'}
          <button onClick={() => navigate(isSignup ? 'login' : 'signup')}>
            {isSignup ? 'Log in' : 'Create account'}
          </button>
        </p>
        {!isSignup && (
          <p className="auth-switch">
            Forgot your password?
            <button onClick={() => navigate('forgot-password')}>Reset it</button>
          </p>
        )}
      </Card>
    </main>
  );
}

function VerifyEmailPage({ otpForm, setOtpForm, error, notice, verifyEmail, navigate }) {
  return (
    <main className="auth-page">
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <span className="brand-mark">N</span>
          <span><strong>Nav</strong></span>
        </button>
        <div className="auth-card-head">
          <div>
            <h2>Verification code</h2>
            <p>Codes expire after 5 minutes.</p>
          </div>
          <Mail size={21} />
        </div>
        {error && <Alert variant="destructive">{error}</Alert>}
        {notice && <Alert variant="success">{notice}</Alert>}
        <form className="auth-form" onSubmit={verifyEmail}>
          <Label>
            Email
            <Input type="email" value={otpForm.email} onChange={event => setOtpForm({ ...otpForm, email: event.target.value })} />
          </Label>
          <Label>
            OTP
            <Input inputMode="numeric" value={otpForm.otp} onChange={event => setOtpForm({ ...otpForm, otp: event.target.value })} />
          </Label>
          <Button type="submit" className="full-button">Verify email</Button>
        </form>
      </Card>
    </main>
  );
}

function PasswordResetPage({ mode, resetForm, setResetForm, error, notice, requestPasswordReset, resetPassword, navigate }) {
  const isReset = mode === 'reset-password';
  return (
    <main className="auth-page">
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <span className="brand-mark">N</span>
          <span><strong>Nav</strong></span>
        </button>
        <h2>{isReset ? 'Set a new password' : 'Reset password'}</h2>
        {error && <Alert variant="destructive">{error}</Alert>}
        {notice && <Alert variant="success">{notice}</Alert>}
        <form className="auth-form" onSubmit={isReset ? resetPassword : requestPasswordReset}>
          <Label>
            Email
            <Input type="email" value={resetForm.email} onChange={event => setResetForm({ ...resetForm, email: event.target.value })} />
          </Label>
          {isReset && (
            <>
              <Label>
                OTP
                <Input inputMode="numeric" value={resetForm.otp} onChange={event => setResetForm({ ...resetForm, otp: event.target.value })} />
              </Label>
              <Label>
                New password
                <Input type="password" value={resetForm.password} onChange={event => setResetForm({ ...resetForm, password: event.target.value })} />
              </Label>
            </>
          )}
          <Button type="submit" className="full-button">{isReset ? 'Set password' : 'Send reset code'}</Button>
        </form>
        {!isReset && <Button variant="ghost" onClick={() => navigate('reset-password')}>I already have a code</Button>}
      </Card>
    </main>
  );
}

function InvitePage({ navigate, isAuthed }) {
  const url = new URL(window.location.href);
  const namespace = url.searchParams.get('namespace') || 'this namespace';
  const email = url.searchParams.get('email') || '';
  return (
    <Page title="Namespace invitation" description={`You were invited to ${namespace}.`}>
      <Card className="section-card">
        <div className="section-heading">
          <div>
            <h2>Create an account to view the namespace</h2>
            <p>{email ? `Use ${email} so the invite can connect to your account.` : 'Sign in or create an account before opening private namespace settings.'}</p>
          </div>
          <div className="button-row">
            {isAuthed ? (
              <Button onClick={() => navigate(`namespace-settings:${namespace}`)}>Open namespace</Button>
            ) : (
              <>
                <Button onClick={() => navigate('signup')}>Create account</Button>
                <Button variant="outline" onClick={() => navigate('login')}>Log in</Button>
              </>
            )}
          </div>
        </div>
      </Card>
    </Page>
  );
}

function HomePage() {
  const installers = [
    {
      name: 'Windows',
      logo: '/windows-applications-svgrepo-com.svg',
      command: `irm ${API}/downloads/nav/install.ps1 | iex`
    },
    {
      name: 'Linux',
      logo: '/linux-svgrepo-com.svg',
      command: `curl -fsSL ${API}/downloads/nav/install.sh | sh`
    },
    {
      name: 'macOS',
      logo: '/apple-logo-svgrepo-com.svg',
      command: `curl -fsSL ${API}/downloads/nav/install.sh | sh`
    }
  ];
  return (
    <Page>
      <section className="home-simple">
        <h1>Nav Registry</h1>
        <p>Packages and managed toolchains for embedded systems.</p>
        <div className="install-grid">
          {installers.map(installer => (
            <Card className="install-card" key={installer.name}>
              <div className="install-header">
                <span className="os-logo-frame">
                  <img className="os-logo" src={installer.logo} alt="" />
                </span>
                <h2>{installer.name}</h2>
              </div>
              <CommandLine command={installer.command} />
            </Card>
          ))}
        </div>
      </section>
    </Page>
  );
}

function PackagesPage({ error, notice, query, setQuery, filteredPackages, downloadPackage, navigate }) {
  const [page, setPage] = useState(1);
  const pageSize = 8;
  const pageCount = Math.max(1, Math.ceil(filteredPackages.length / pageSize));
  const visiblePackages = filteredPackages.slice((page - 1) * pageSize, page * pageSize);
  useEffect(() => setPage(1), [query]);
  useEffect(() => setPage(current => Math.min(current, pageCount)), [pageCount]);
  return (
    <Page title="Packages" description="Browse public packages and install them with Nav.">
      <PageAlerts error={error} notice={notice} />
      <Card className="section-card">
        <div className="section-heading">
          <div>
            <h2>Registry Packages</h2>
            <p>Install commands, version metadata, and public package documentation.</p>
          </div>
          <div className="search-box">
            <Search size={17} />
            <Input value={query} onChange={event => setQuery(event.target.value)} placeholder="Search packages..." />
          </div>
        </div>
        <div className="package-list">
          {visiblePackages.map(pkg => (
            <PackageCard key={`${pkg.namespace}/${pkg.slug}`} pkg={pkg} onDownload={downloadPackage} navigate={navigate} />
          ))}
          {filteredPackages.length === 0 && <div className="empty-state">No packages match this search.</div>}
        </div>
        {filteredPackages.length > 0 && <Pagination page={page} pageCount={pageCount} onPageChange={setPage} />}
      </Card>
    </Page>
  );
}

function NamespacesPage({
  error,
  notice,
  isAuthed,
  navigate,
  namespaces,
  namespaceForm,
  setNamespaceForm,
  createNamespace
}) {
  return (
    <Page title="Namespaces" description="Create and manage registry ownership scopes.">
      <PageAlerts error={error} notice={notice} />
      {!isAuthed ? (
        <Card className="section-card">
          <div className="section-heading">
            <div>
              <h2>Sign in required</h2>
              <p>Namespaces are account-owned and require authentication.</p>
            </div>
            <Button onClick={() => navigate('login')}>Log in</Button>
          </div>
        </Card>
      ) : (
        <div className="settings-layout">
          <aside className="settings-nav">
            <button className="active">Namespaces</button>
            <button onClick={() => navigate('settings')}>Account settings</button>
          </aside>
          <section className="settings-main">
            <Card className="section-card">
              <div className="section-heading">
                <div>
                  <h2>Create namespace</h2>
                  <p>Namespaces group packages and maintainers.</p>
                </div>
              </div>
              <form className="inline-form" onSubmit={createNamespace}>
                <Label>
                  Name
                  <Input value={namespaceForm.name} onChange={event => setNamespaceForm({ name: event.target.value })} />
                </Label>
                <Button type="submit">Create namespace</Button>
              </form>
            </Card>

            <Card className="section-card">
              <h2>Your namespaces</h2>
              <div className="repo-list">
                {namespaces.map(ns => (
                  <article key={ns.id} className="repo-row">
                    <div>
                      <h3>{ns.name}</h3>
                      <p>{ns.role} - {ns.packages} packages - {ns.members} members</p>
                    </div>
                    <Button variant="outline" onClick={() => navigate(`namespace-settings:${ns.name}`)}>Settings</Button>
                  </article>
                ))}
                {namespaces.length === 0 && <div className="empty-state">No namespaces yet.</div>}
              </div>
            </Card>
          </section>
        </div>
      )}
    </Page>
  );
}

function NamespaceSettingsPage({
  route,
  error,
  notice,
  namespaces,
  members,
  filteredPackages,
  memberForm,
  setMemberForm,
  packageForm,
  setPackageForm,
  addNamespaceMember,
  createPackage,
  auditLogs,
  navigate,
  emailStatus
}) {
  const { namespace } = routeParams(route);
  const current = namespaces.find(ns => ns.name === namespace);
  const namespacePackages = filteredPackages.filter(pkg => pkg.namespace === namespace);
  useEffect(() => {
    setMemberForm(currentForm => ({ ...currentForm, namespace }));
    setPackageForm(currentForm => ({ ...currentForm, namespace }));
  }, [namespace]);
  return (
    <Page title={namespace} description="Namespace settings, members, permissions, and audit trail.">
      <PageAlerts error={error} notice={notice} />
      <div className="settings-layout">
        <aside className="settings-nav">
          <button onClick={() => navigate('namespaces')}>Namespaces</button>
          <button className="active">Members</button>
          <button onClick={() => navigate('settings')}>Account settings</button>
        </aside>
        <section className="settings-main">
          <Card className="section-card">
            <div className="section-heading">
              <div>
                <h2>Namespace settings</h2>
                <p>{current ? `${current.role} access - ${current.packages} packages` : 'Namespace metadata'}</p>
              </div>
            </div>
          </Card>

          <Card className="section-card">
            <div className="section-heading">
              <div>
                <h2>Packages</h2>
                <p>Packages owned by this namespace.</p>
              </div>
              <Button variant="outline" onClick={() => navigate('packages')}>Browse registry</Button>
            </div>
            <div className="repo-list">
              {namespacePackages.map(pkg => (
                <article className="repo-row" key={`${pkg.namespace}/${pkg.slug}`}>
                  <div>
                    <h3>{pkg.slug}</h3>
                    <p>{pkg.description || 'No description'} - latest {pkg.latest_version || 'unpublished'}</p>
                  </div>
                  <div className="button-row">
                    <Button variant="outline" size="sm" onClick={() => navigate(`package-detail:${pkg.namespace}:${pkg.slug}`)}>Open</Button>
                    <Button variant="outline" size="sm" onClick={() => navigate(`package-upload:${pkg.namespace}:${pkg.slug}`)}>Publish version</Button>
                  </div>
                </article>
              ))}
              {namespacePackages.length === 0 && <div className="empty-state">No packages in this namespace yet.</div>}
            </div>
          </Card>

          <Card className="section-card">
            <div className="section-heading">
              <div>
                <h2>Create package</h2>
                <p>Create package metadata inside this namespace, then publish versions from the package page.</p>
              </div>
            </div>
            <form className="inline-form" onSubmit={createPackage}>
              <Label>
                Package name
                <Input value={packageForm.slug} onChange={event => setPackageForm({ ...packageForm, slug: event.target.value })} />
              </Label>
              <Label>
                Description
                <Input value={packageForm.description} onChange={event => setPackageForm({ ...packageForm, description: event.target.value })} />
              </Label>
              <Button type="submit">Create package</Button>
            </form>
          </Card>

          <Card className="section-card">
            <div className="section-heading">
              <div>
                <h2>Members</h2>
                <p>Invite maintainers, admins, or viewers to this namespace.</p>
              </div>
            </div>
            <form className="inline-form" onSubmit={addNamespaceMember}>
              <Label>
                Email
                <Input type="email" value={memberForm.email} onChange={event => setMemberForm({ ...memberForm, email: event.target.value })} />
              </Label>
              <Label>
                Role
                <select className="input" value={memberForm.role} onChange={event => setMemberForm({ ...memberForm, role: event.target.value })}>
                  <option value="maintainer">maintainer</option>
                  <option value="admin">admin</option>
                  <option value="viewer">viewer</option>
                </select>
              </Label>
              <Button type="submit">Add member</Button>
            </form>
            <div className="repo-list">
              {members.map(member => (
                <article key={member.id} className="repo-row">
                  <div>
                    <h3>{member.email}</h3>
                    <p>{member.role} - {member.accepted_at ? 'accepted' : 'invited'}</p>
                  </div>
                  <Badge variant="outline">{member.role}</Badge>
                </article>
              ))}
            </div>
          </Card>

          <Card className="section-card">
            <h2>Audit log</h2>
            <div className="audit-list">
              {auditLogs.slice(0, 12).map((log, index) => (
                <div key={`${log.action}-${log.created_at}-${index}`}>
                  <Badge>{log.action}</Badge>
                  <code>{new Date(log.created_at).toLocaleString()}</code>
                </div>
              ))}
            </div>
          </Card>
        </section>
      </div>
    </Page>
  );
}

function MaintainerPage(props) {
  return (
    <Page title="Maintainer" description="Create namespaces, invite members, publish package archives, and inspect audit events.">
      <PageAlerts error={props.error} notice={props.notice} />
      {props.isAuthed ? (
        <MaintainerConsole {...props} />
      ) : (
        <Card className="section-card">
          <div className="section-heading">
            <div>
              <h2>Maintainer Console</h2>
              <p>Log in to create namespaces, invite members, create packages, and publish archive blobs.</p>
            </div>
            <Button onClick={() => props.navigate('login')}>Log in</Button>
          </div>
        </Card>
      )}
    </Page>
  );
}

function ToolchainsPage({
  stats,
  activeVendor,
  setActiveVendor,
  filteredToolchainGroups,
  toolchainFilters,
  setToolchainFilters,
  toolchainFilterOptions,
  showToolchainFilters,
  setShowToolchainFilters
}) {
  const [page, setPage] = useState(1);
  const pageSize = 9;
  const pageCount = Math.max(1, Math.ceil(filteredToolchainGroups.length / pageSize));
  const visibleToolchains = filteredToolchainGroups.slice((page - 1) * pageSize, page * pageSize);
  const compilerCount = filteredToolchainGroups.filter(toolchain => /gcc|clang|compiler|xtensa|riscv|arm-none-eabi/i.test(`${toolchain.name} ${toolchain.description}`)).length;
  const platformCount = new Set(filteredToolchainGroups.flatMap(toolchain => toolchain.artifacts
    .filter(artifact => artifact.os && artifact.arch)
    .map(artifact => `${artifact.os}/${artifact.arch}`))).size;
  const vendorCount = new Set(filteredToolchainGroups.map(toolchain => toolchain.vendor)).size;
  useEffect(() => setPage(1), [activeVendor, toolchainFilters.os, toolchainFilters.arch, toolchainFilters.board]);
  useEffect(() => setPage(current => Math.min(current, pageCount)), [pageCount]);
  return (
    <Page title="Toolchains">
      <section className="stats-grid toolchain-summary">
        <Card className="stat-card">
          <Package size={18} />
          <span>Packages</span>
          <strong>{stats?.packages ?? 0}</strong>
        </Card>
        <Card className="stat-card">
          <Wrench size={18} />
          <span>Toolchains</span>
          <strong>{stats?.toolchains ?? 0}</strong>
        </Card>
        <Card className="stat-card">
          <CircuitBoard size={18} />
          <span>Compilers</span>
          <strong>{compilerCount}</strong>
        </Card>
        <Card className="stat-card">
          <Monitor size={18} />
          <span>Platforms</span>
          <strong>{platformCount}</strong>
        </Card>
        <Card className="stat-card">
          <Globe2 size={18} />
          <span>Vendors</span>
          <strong>{vendorCount}</strong>
        </Card>
      </section>
      <Card className="section-card">
        <div className="section-heading">
          <div>
            <h2>Managed Toolchains</h2>
            <p>Grouped by toolchain version, with platform artifacts and official source links.</p>
          </div>
          <div className="toolbar-actions">
            <div className="tabs">
              {['all', 'nav', 'official', 'hardware'].map(vendor => (
                <button key={vendor} className={activeVendor === vendor ? 'active' : ''} onClick={() => setActiveVendor(vendor)}>
                  {vendor}
                </button>
              ))}
            </div>
            <Button variant="outline" size="sm" onClick={() => setShowToolchainFilters(!showToolchainFilters)}>
              {showToolchainFilters ? <X size={16} /> : null}
              {showToolchainFilters ? 'Close filters' : 'Filters'}
            </Button>
          </div>
        </div>
        {showToolchainFilters && (
          <div className="filter-panel">
            <Label>
              Operating system
              <select className="input" value={toolchainFilters.os} onChange={event => setToolchainFilters({ ...toolchainFilters, os: event.target.value })}>
                <option value="all">All OS</option>
                {toolchainFilterOptions.os.map(os => <option value={os} key={os}>{os}</option>)}
              </select>
            </Label>
            <Label>
              Architecture
              <select className="input" value={toolchainFilters.arch} onChange={event => setToolchainFilters({ ...toolchainFilters, arch: event.target.value })}>
                <option value="all">All architectures</option>
                {toolchainFilterOptions.arch.map(arch => <option value={arch} key={arch}>{arch}</option>)}
              </select>
            </Label>
            <Label>
              Board family
              <select className="input" value={toolchainFilters.board} onChange={event => setToolchainFilters({ ...toolchainFilters, board: event.target.value })}>
                <option value="all">All boards</option>
                {toolchainFilterOptions.boards.map(board => <option value={board} key={board}>{board}</option>)}
              </select>
            </Label>
          </div>
        )}
        <div className="toolchain-grid">
          {visibleToolchains.map(toolchain => (
            <ToolchainCard
              key={`${toolchain.vendor_kind}-${toolchain.vendor}-${toolchain.name}-${toolchain.version}`}
              toolchain={toolchain}
            />
          ))}
          {filteredToolchainGroups.length === 0 && <div className="empty-state">No toolchains match this filter.</div>}
        </div>
        {filteredToolchainGroups.length > 0 && <Pagination page={page} pageCount={pageCount} onPageChange={setPage} />}
      </Card>
    </Page>
  );
}

function PackageDetailPage({ route, filteredPackages, downloadPackage, navigate, request, isAuthed }) {
  const { namespace, name } = routeParams(route);
  const pkg = filteredPackages.find(item => item.namespace === namespace && item.slug === name);
  const [details, setDetails] = useState(null);
  const [detailError, setDetailError] = useState('');
  const [activeTab, setActiveTab] = useState('package');

  useEffect(() => {
    let alive = true;
    setDetailError('');
    request(`/packages/${namespace}/${name}`)
      .then(data => {
        if (alive) setDetails(data);
      })
      .catch(error => {
        if (alive) setDetailError(error.message);
      });
    return () => {
      alive = false;
    };
  }, [namespace, name]);

  useEffect(() => {
    setActiveTab('package');
  }, [namespace, name]);

  if (!pkg) {
    return (
      <Page title={`${namespace}/${name}`} description="Package details">
        <div className="empty-state">Package not found in the loaded registry data.</div>
      </Page>
    );
  }

  return (
    <Page title={`${pkg.namespace}/${pkg.slug}`} description={pkg.description}>
      {detailError && <Alert variant="destructive">{detailError}</Alert>}
      <div className="repo-layout">
        <aside className="settings-nav">
          <button className={activeTab === 'package' ? 'active' : ''} onClick={() => setActiveTab('package')}>Package</button>
          <button className={activeTab === 'versions' ? 'active' : ''} onClick={() => setActiveTab('versions')}>Versions</button>
          {isAuthed && <button onClick={() => navigate(`package-upload:${pkg.namespace}:${pkg.slug}`)}>Publish version</button>}
        </aside>
        <section className="settings-main">
          {activeTab === 'package' ? (
            <>
              <Card className="section-card package-hero-card">
                <div className="section-heading">
                  <div>
                    <h2>Install</h2>
                    <p>Use Nav CLI to add this package to an embedded project.</p>
                  </div>
                  <Button variant="outline" onClick={() => downloadPackage(pkg)}>Download archive</Button>
                </div>
                <div className="command-stack">
                  <CommandLine command={`nav install ${pkg.namespace}/${pkg.slug}${pkg.latest_version ? `@${pkg.latest_version}` : ''}`} />
                  <CommandLine command={`nav info ${pkg.namespace}/${pkg.slug}`} />
                  <CommandLine command={`nav docs ${pkg.namespace}/${pkg.slug}`} />
                </div>
              </Card>

              <div className="package-overview-grid">
                <Card className="section-card">
                  <h2>Project details</h2>
                  <div className="metadata-list">
                    <div><span>Latest version</span><strong>{pkg.latest_version || 'Unpublished'}</strong></div>
                    <div><span>License</span><strong>{pkg.license || 'Not specified'}</strong></div>
                    <div><span>Visibility</span><strong>Public</strong></div>
                    <div><span>Downloads</span><strong>{pkg.total_downloads ?? 0}</strong></div>
                  </div>
                </Card>
                <Card className="section-card">
                  <h2>Package commands</h2>
                  <div className="command-stack">
                    <CommandLine command={`nav search ${pkg.slug}`} />
                    <CommandLine command={`nav install ${pkg.namespace}/${pkg.slug}`} />
                    <CommandLine command={`nav update ${pkg.namespace}/${pkg.slug}`} />
                  </div>
                </Card>
              </div>

              <Card className="section-card">
                <h2>Maintainers</h2>
                <div className="repo-list">
                  {(details?.maintainers || []).map((maintainer, index) => (
                    <article className="repo-row" key={`${maintainer.name}-${maintainer.role}-${index}`}>
                      <div>
                        <h3>{maintainer.name || 'Nav maintainer'}</h3>
                      </div>
                      <Badge variant="outline">{maintainer.role}</Badge>
                    </article>
                  ))}
                  {!details?.maintainers?.length && <div className="empty-state">No maintainers loaded.</div>}
                </div>
              </Card>
            </>
          ) : (
            <Card className="section-card">
              <div className="section-heading">
                <div>
                  <h2>Version history</h2>
                  <p>Immutable public releases with maintainer changelogs.</p>
                </div>
                {isAuthed && <Button variant="outline" onClick={() => navigate(`package-upload:${pkg.namespace}:${pkg.slug}`)}>Publish version</Button>}
              </div>
              <div className="version-list">
                {(details?.versions || []).map(version => (
                  <article className="version-row" key={version.version}>
                    <div>
                      <h3>{version.version}</h3>
                      <p>{new Date(version.created_at).toLocaleString()} - {version.downloads ?? 0} downloads - {formatBytes(version.size_bytes)}</p>
                    </div>
                    <div className="version-changelog">
                      <strong>Changelog</strong>
                      <p>{version.changelog || version.manifest?.changelog || 'No changelog provided.'}</p>
                    </div>
                    <code>{version.sha256 ? `sha256-${version.sha256.slice(0, 32)}...` : 'pending'}</code>
                  </article>
                ))}
                {!details?.versions?.length && <div className="empty-state">No published versions yet.</div>}
              </div>
            </Card>
          )}
        </section>
      </div>
    </Page>
  );
}

function PackageUploadPage({
  route,
  error,
  notice,
  publishForm,
  setPublishForm,
  publishVersion,
  navigate
}) {
  const { namespace, name } = routeParams(route);
  useEffect(() => {
    setPublishForm(current => ({ ...current, namespace, package: name }));
  }, [namespace, name]);

  return (
    <Page title={`${namespace}/${name}`} description="Publish a new immutable package version.">
      <PageAlerts error={error} notice={notice} />
      <div className="repo-layout">
        <aside className="settings-nav">
          <button onClick={() => navigate(`package-detail:${namespace}:${name}`)}>Code</button>
          <button className="active">Publish version</button>
          <button onClick={() => navigate(`namespace-settings:${namespace}`)}>Namespace settings</button>
        </aside>
        <section className="settings-main">
          <Card className="section-card">
            <div className="section-heading">
              <div>
                <h2>Upload package archive</h2>
                <p>The archive is stored in MinIO and connected through package version metadata.</p>
              </div>
            </div>
            <form className="stack-form" onSubmit={publishVersion}>
              <Label>
                Namespace
                <Input value={publishForm.namespace} onChange={event => setPublishForm({ ...publishForm, namespace: event.target.value })} />
              </Label>
              <Label>
                Package
                <Input value={publishForm.package} onChange={event => setPublishForm({ ...publishForm, package: event.target.value })} />
              </Label>
              <Label>
                Version
                <Input value={publishForm.version} onChange={event => setPublishForm({ ...publishForm, version: event.target.value })} />
              </Label>
              <Label>
                Archive blob
                <Input type="file" onChange={event => setPublishForm({ ...publishForm, file: event.target.files?.[0] || null })} />
              </Label>
              <Label>
                Changelog
                <Textarea
                  rows={5}
                  placeholder="Describe what changed in this version."
                  value={publishForm.changelogText}
                  onChange={event => setPublishForm({ ...publishForm, changelogText: event.target.value })}
                />
              </Label>
              <Label>
                Changelog file
                <Input type="file" accept=".md,.txt,text/markdown,text/plain" onChange={event => setPublishForm({ ...publishForm, changelogFile: event.target.files?.[0] || null })} />
              </Label>
              <Button type="submit">Upload and publish</Button>
            </form>
          </Card>
        </section>
      </div>
    </Page>
  );
}

function AccountSettingsPage({ user, auditLogs }) {
  return (
    <Page title="Account settings" description="Profile and recent account activity.">
      <div className="settings-layout">
        <aside className="settings-nav">
          <button className="active">Profile</button>
        </aside>
        <section className="settings-main">
          <Card className="section-card">
            <h2>Profile</h2>
            <div className="repo-list">
              <article className="repo-row">
                <div>
                  <h3>{user?.name || 'Signed out'}</h3>
                  <p>{user?.email || 'Log in to view account details.'}</p>
                </div>
              </article>
            </div>
          </Card>

          <Card className="section-card">
            <h2>Recent activity</h2>
            <div className="audit-list">
              {auditLogs.slice(0, 8).map((log, index) => (
                <div key={`${log.action}-${log.created_at}-${index}`}>
                  <Badge>{log.action}</Badge>
                  <code>{new Date(log.created_at).toLocaleString()}</code>
                </div>
              ))}
              {auditLogs.length === 0 && <div className="empty-state">No account activity yet.</div>}
            </div>
          </Card>
        </section>
      </div>
    </Page>
  );
}

function AdminPage({
  error,
  notice,
  toolchainPublishForm,
  setToolchainPublishForm,
  publishToolchain,
  navigate
}) {
  return (
    <Page title="Registry administration">
      <PageAlerts error={error} notice={notice} />
      <Card className="section-card">
        <div className="section-heading">
          <h2>Official packages</h2>
          <Button variant="outline" onClick={() => navigate('namespace-settings:nav')}>Open nav namespace</Button>
        </div>
      </Card>
      <Card className="section-card">
        <h2>Publish managed toolchain</h2>
        <form className="admin-form" onSubmit={publishToolchain}>
          <Label>
            Vendor type
            <select className="input" value={toolchainPublishForm.vendor_kind} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, vendor_kind: event.target.value })}>
              <option value="nav">Nav</option>
              <option value="official">Official compiler</option>
              <option value="hardware">Hardware vendor</option>
            </select>
          </Label>
          <Label>
            Vendor name
            <Input value={toolchainPublishForm.vendor_name} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, vendor_name: event.target.value, vendor_slug: event.target.value })} />
          </Label>
          <Label>
            Toolchain name
            <Input value={toolchainPublishForm.name} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, name: event.target.value })} />
          </Label>
          <Label>
            Version
            <Input value={toolchainPublishForm.version} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, version: event.target.value })} />
          </Label>
          <Label>
            Operating system
            <select className="input" value={toolchainPublishForm.os} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, os: event.target.value })}>
              <option value="windows">Windows</option>
              <option value="linux">Linux</option>
              <option value="darwin">macOS</option>
            </select>
          </Label>
          <Label>
            Architecture
            <select className="input" value={toolchainPublishForm.arch} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, arch: event.target.value })}>
              <option value="x64">x64</option>
              <option value="arm64">arm64</option>
            </select>
          </Label>
          <Label className="admin-span">
            Description
            <Input value={toolchainPublishForm.description} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, description: event.target.value })} />
          </Label>
          <Label>
            Official source URL
            <Input type="url" value={toolchainPublishForm.homepage_url} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, homepage_url: event.target.value })} />
          </Label>
          <Label>
            Upstream archive URL
            <Input type="url" value={toolchainPublishForm.upstream_url} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, upstream_url: event.target.value })} />
          </Label>
          <Label>
            Signature
            <Input value={toolchainPublishForm.signature} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, signature: event.target.value })} />
          </Label>
          <Label>
            Archive
            <Input type="file" onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, file: event.target.files?.[0] || null })} />
          </Label>
          <Button type="submit">Publish toolchain</Button>
        </form>
      </Card>
    </Page>
  );
}

function Page({ title, description, children }) {
  return (
    <section className="page">
      {title && (
        <header className="page-header">
          <h1>{title}</h1>
          {description && <p>{description}</p>}
        </header>
      )}
      <div className="page-content">{children}</div>
    </section>
  );
}

function PageAlerts({ error, notice }) {
  return (
    <>
      {error && <Alert variant="destructive">{error}</Alert>}
      {notice && <Alert variant="success">{notice}</Alert>}
    </>
  );
}

function StorageTable({ objects }) {
  return (
    <Table
      columns={{ labels: ['Kind', 'Object key', 'Size'], grid: '140px minmax(0, 1fr) 120px' }}
      rows={objects.map(object => ({ ...object, key: `${object.storage_bucket}/${object.storage_key}` }))}
      empty="No storage objects found."
      renderRow={object => (
        <>
          <span><Badge>{object.kind}</Badge></span>
          <code>{object.storage_bucket}/{object.storage_key}</code>
          <span>{formatBytes(object.size_bytes)}</span>
        </>
      )}
    />
  );
}

function MaintainerConsole({
  namespaces,
  members,
  auditLogs,
  namespaceForm,
  setNamespaceForm,
  memberForm,
  setMemberForm,
  packageForm,
  setPackageForm,
  publishForm,
  setPublishForm,
  createNamespace,
  addNamespaceMember,
  createPackage,
  publishVersion
}) {
  return (
    <Card className="section-card maintainer-console">
      <div className="section-heading">
        <div>
          <h2>Maintainer Console</h2>
          <p>Create the same objects a production registry maintainer would manage.</p>
        </div>
        <Badge variant="outline">end-to-end write flow</Badge>
      </div>

      <div className="console-grid">
        <form className="console-panel" onSubmit={createNamespace}>
          <h3>1. Namespace</h3>
          <Label>
            Namespace name
            <Input value={namespaceForm.name} onChange={event => setNamespaceForm({ name: event.target.value })} />
          </Label>
          <Button type="submit">Create namespace</Button>
        </form>

        <form className="console-panel" onSubmit={addNamespaceMember}>
          <h3>2. Members</h3>
          <Label>
            Namespace
            <Input value={memberForm.namespace} onChange={event => setMemberForm({ ...memberForm, namespace: event.target.value })} />
          </Label>
          <Label>
            Email
            <Input type="email" value={memberForm.email} onChange={event => setMemberForm({ ...memberForm, email: event.target.value })} />
          </Label>
          <Label>
            Role
            <select className="input" value={memberForm.role} onChange={event => setMemberForm({ ...memberForm, role: event.target.value })}>
              <option value="maintainer">maintainer</option>
              <option value="admin">admin</option>
              <option value="viewer">viewer</option>
            </select>
          </Label>
          <Button type="submit">Add member</Button>
        </form>

        <form className="console-panel" onSubmit={createPackage}>
          <h3>3. Package</h3>
          <Label>
            Namespace
            <Input value={packageForm.namespace} onChange={event => setPackageForm({ ...packageForm, namespace: event.target.value })} />
          </Label>
          <Label>
            Package slug
            <Input value={packageForm.slug} onChange={event => setPackageForm({ ...packageForm, slug: event.target.value })} />
          </Label>
          <Label>
            Description
            <Input value={packageForm.description} onChange={event => setPackageForm({ ...packageForm, description: event.target.value })} />
          </Label>
          <Button type="submit">Create package</Button>
        </form>

        <form className="console-panel" onSubmit={publishVersion}>
          <h3>4. Publish Version</h3>
          <Label>
            Namespace
            <Input value={publishForm.namespace} onChange={event => setPublishForm({ ...publishForm, namespace: event.target.value })} />
          </Label>
          <Label>
            Package
            <Input value={publishForm.package} onChange={event => setPublishForm({ ...publishForm, package: event.target.value })} />
          </Label>
          <Label>
            Version
            <Input value={publishForm.version} onChange={event => setPublishForm({ ...publishForm, version: event.target.value })} />
          </Label>
          <Label>
            Archive blob
            <Input type="file" onChange={event => setPublishForm({ ...publishForm, file: event.target.files?.[0] || null })} />
          </Label>
          <Label>
            Changelog
            <Textarea
              rows={4}
              placeholder="What changed in this version?"
              value={publishForm.changelogText}
              onChange={event => setPublishForm({ ...publishForm, changelogText: event.target.value })}
            />
          </Label>
          <Label>
            Changelog file
            <Input type="file" accept=".md,.txt,text/markdown,text/plain" onChange={event => setPublishForm({ ...publishForm, changelogFile: event.target.files?.[0] || null })} />
          </Label>
          <Button type="submit">Upload and publish</Button>
        </form>
      </div>

      <div className="console-grid two">
        <div className="console-panel">
          <h3>Your namespaces</h3>
          <div className="compact-list">
            {namespaces.map(ns => (
              <div key={ns.id}>
                <strong>{ns.name}</strong>
                <span>{ns.role} - {ns.packages} packages - {ns.members} members</span>
              </div>
            ))}
            {namespaces.length === 0 && <small>No namespaces yet.</small>}
          </div>
        </div>

        <div className="console-panel">
          <h3>Selected namespace members</h3>
          <div className="compact-list">
            {members.map(member => (
              <div key={member.id}>
                <strong>{member.email}</strong>
                <span>{member.role} - {member.accepted_at ? 'accepted' : 'invited'}</span>
              </div>
            ))}
            {members.length === 0 && <small>No members loaded.</small>}
          </div>
        </div>
      </div>

      <div className="console-panel">
        <h3>Recent audit events</h3>
        <div className="audit-list">
          {auditLogs.slice(0, 8).map((log, index) => (
            <div key={`${log.action}-${log.created_at}-${index}`}>
              <Badge>{log.action}</Badge>
              <code>{new Date(log.created_at).toLocaleString()}</code>
            </div>
          ))}
          {auditLogs.length === 0 && <small>No audit events yet.</small>}
        </div>
      </div>
    </Card>
  );
}

function PackageCard({ pkg, onDownload, navigate }) {
  const installCommand = `nav install ${pkg.namespace}/${pkg.slug}${pkg.latest_version ? `@${pkg.latest_version}` : ''}`;
  return (
    <article className="package-card">
      <div className="package-main">
        <div className="package-icon"><Archive size={20} /></div>
        <div>
          <h3>{pkg.namespace}/{pkg.slug}</h3>
          <p>{pkg.description}</p>
          <div className="package-meta">
            <Badge variant="outline">{pkg.latest_version ? `v${pkg.latest_version}` : 'unpublished'}</Badge>
            <Badge>{pkg.license || 'No license'}</Badge>
            <span><Globe2 size={14} /> public</span>
          </div>
          <div className="command-stack">
            <CommandLine command={installCommand} />
          </div>
        </div>
      </div>
      <div className="package-actions">
        <Button variant="outline" size="sm" onClick={() => navigate(`package-detail:${pkg.namespace}:${pkg.slug}`)}>
          Open package
        </Button>
        <Button variant="outline" size="sm" onClick={() => onDownload(pkg)} disabled={!pkg.latest_version}>
          <Download size={15} />
          Download
        </Button>
      </div>
    </article>
  );
}

function Pagination({ page, pageCount, onPageChange }) {
  return (
    <nav className="pagination" aria-label="Pagination">
      <Button variant="outline" size="sm" disabled={page <= 1} onClick={() => onPageChange(page - 1)}>Previous</Button>
      <span>Page {page} of {pageCount}</span>
      <Button variant="outline" size="sm" disabled={page >= pageCount} onClick={() => onPageChange(page + 1)}>Next</Button>
    </nav>
  );
}

function ToolchainCard({ toolchain }) {
  const platforms = toolchain.artifacts
    .filter(artifact => artifact.os && artifact.arch)
    .map(artifact => `${artifact.os}/${artifact.arch}`);
  return (
    <article className="toolchain-card">
      <div className="toolchain-head">
        <div className="toolchain-icon">
          {toolchain.vendor_kind === 'hardware' ? <CircuitBoard size={18} /> : <Wrench size={18} />}
        </div>
        <div>
          <h3>{toolchain.name}</h3>
          <p>{toolchain.description}</p>
        </div>
      </div>
      <div className="chip-row">
        <Badge variant={toolchain.vendor_kind === 'nav' ? 'default' : 'secondary'}>{toolchain.vendor_kind}</Badge>
        <Badge variant="outline">{toolchain.vendor}</Badge>
        {toolchain.boards.slice(0, 2).map(board => <Badge variant="outline" key={board}>{board}</Badge>)}
      </div>
      <div className="metadata-list compact">
        <div><span>Version</span><strong>{toolchain.version}</strong></div>
        <div><span>Platforms</span><strong>{platforms.length ? platforms.join(', ') : 'Not mirrored yet'}</strong></div>
      </div>
      <div className="button-row">
        {toolchain.official_url && (
          <a className="btn btn-outline btn-sm" href={toolchain.official_url} target="_blank" rel="noreferrer">
            Official source
          </a>
        )}
      </div>
    </article>
  );
}

function WorkflowItem({ icon: Icon, title, text }) {
  return (
    <div className="workflow-item">
      <div><Icon size={15} /></div>
      <span>
        <strong>{title}</strong>
        <small>{text}</small>
      </span>
    </div>
  );
}

function getToolchainBoards(toolchain) {
  const manifest = toolchain.manifest || {};
  const raw = [
    ...(Array.isArray(manifest.targets) ? manifest.targets : []),
    ...(Array.isArray(manifest.boards) ? manifest.boards : []),
    manifest.board,
    manifest.target
  ].filter(Boolean);
  const name = `${toolchain.name} ${toolchain.description}`.toLowerCase();
  if (name.includes('esp32') || name.includes('espressif')) raw.push('esp32');
  if (name.includes('stm32') || name.includes('stlink')) raw.push('stm32');
  if (name.includes('arm-none-eabi')) raw.push('cortex-m');
  return [...new Set(raw.map(item => String(item).toLowerCase()))];
}

function groupToolchains(toolchains) {
  const groups = new Map();
  for (const artifact of toolchains) {
    const key = `${artifact.vendor_kind}:${artifact.vendor}:${artifact.name}:${artifact.version}`;
    if (!groups.has(key)) {
      groups.set(key, {
        ...artifact,
        artifacts: [],
        boards: [],
        official_url: artifact.homepage_url || artifact.upstream_url || artifact.provenance?.homepage_url || artifact.provenance?.source_url || ''
      });
    }
    const group = groups.get(key);
    group.artifacts.push({
      os: artifact.os,
      arch: artifact.arch,
      size_bytes: artifact.size_bytes,
      sha256: artifact.sha256,
      upstream_url: artifact.upstream_url
    });
    group.boards = [...new Set([...group.boards, ...getToolchainBoards(artifact)])];
    if (!group.official_url) {
      group.official_url = artifact.upstream_url || '';
    }
  }
  return [...groups.values()].sort((a, b) => `${a.vendor_kind}:${a.name}`.localeCompare(`${b.vendor_kind}:${b.name}`));
}

function normalizeRoute(pathname) {
  if (pathname.includes('signup')) return 'signup';
  if (pathname.includes('login')) return 'login';
  if (pathname.includes('verify-email')) return 'verify-email';
  if (pathname.includes('forgot-password')) return 'forgot-password';
  if (pathname.includes('reset-password')) return 'reset-password';
  if (pathname.includes('invite')) return 'invite';
  if (pathname === '/settings') return 'settings';
  const namespaceMatch = pathname.match(/^\/namespaces\/([^/]+)\/settings/);
  if (namespaceMatch) return `namespace-settings:${decodeURIComponent(namespaceMatch[1])}`;
  if (pathname.includes('namespaces')) return 'namespaces';
  const packageUploadMatch = pathname.match(/^\/packages\/([^/]+)\/([^/]+)\/upload/);
  if (packageUploadMatch) return `package-upload:${decodeURIComponent(packageUploadMatch[1])}:${decodeURIComponent(packageUploadMatch[2])}`;
  const packageDetailMatch = pathname.match(/^\/packages\/([^/]+)\/([^/]+)/);
  if (packageDetailMatch) return `package-detail:${decodeURIComponent(packageDetailMatch[1])}:${decodeURIComponent(packageDetailMatch[2])}`;
  if (pathname.includes('packages')) return 'packages';
  if (pathname.includes('toolchains')) return 'toolchains';
  if (pathname.includes('admin')) return 'admin';
  if (pathname.includes('cli')) return 'home';
  if (pathname.includes('security')) return 'toolchains';
  return 'home';
}

function routeToPath(route) {
  if (route === 'home') return '/';
  if (route === 'settings') return '/settings';
  if (route === 'verify-email') return '/verify-email';
  if (route === 'forgot-password') return '/forgot-password';
  if (route === 'reset-password') return '/reset-password';
  if (route === 'invite') return '/invite';
  if (route.startsWith('namespace-settings:')) {
    const { namespace } = routeParams(route);
    return `/namespaces/${encodeURIComponent(namespace)}/settings`;
  }
  if (route.startsWith('package-upload:')) {
    const { namespace, name } = routeParams(route);
    return `/packages/${encodeURIComponent(namespace)}/${encodeURIComponent(name)}/upload`;
  }
  if (route.startsWith('package-detail:')) {
    const { namespace, name } = routeParams(route);
    return `/packages/${encodeURIComponent(namespace)}/${encodeURIComponent(name)}`;
  }
  return `/${route}`;
}

function routeKey(route) {
  if (route.startsWith('package-')) return 'packages';
  if (route.startsWith('namespace-')) return 'namespaces';
  return route;
}

function routeParams(route) {
  const [, namespace, name] = route.split(':');
  return { namespace, name };
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

async function sha256Hex(arrayBuffer) {
  const hash = await crypto.subtle.digest('SHA-256', arrayBuffer);
  return [...new Uint8Array(hash)].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

createRoot(document.getElementById('root')).render(<App />);
