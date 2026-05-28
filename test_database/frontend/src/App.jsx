import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Select as SelectPrimitive } from 'radix-ui';
import {
  Activity,
  Archive,
  ArrowLeft,
  Boxes,
  Check,
  CheckCircle2,
  ChevronDown,
  ChevronRight,
  ChevronUp,
  CircuitBoard,
  Copy,
  Database,
  Download,
  Eye,
  EyeOff,
  Globe2,
  House,
  KeyRound,
  LockKeyhole,
  Mail,
  Monitor,
  Package,
  Pencil,
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

function GoogleLogo() {
  return (
    <svg className="oauth-brand-icon" viewBox="0 0 24 24" aria-hidden="true">
      <path fill="#4285F4" d="M22.56 12.25c0-.78-.07-1.53-.2-2.25H12v4.26h5.92c-.26 1.37-1.04 2.53-2.21 3.31v2.77h3.57c2.08-1.92 3.28-4.74 3.28-8.09z" />
      <path fill="#34A853" d="M12 23c2.97 0 5.46-.98 7.28-2.66l-3.57-2.77c-.98.66-2.23 1.06-3.71 1.06-2.86 0-5.29-1.93-6.16-4.53H2.18v2.84C3.99 20.53 7.7 23 12 23z" />
      <path fill="#FBBC05" d="M5.84 14.1c-.22-.66-.35-1.36-.35-2.1s.13-1.44.35-2.1V7.06H2.18C1.43 8.55 1 10.22 1 12s.43 3.45 1.18 4.94l3.66-2.84z" />
      <path fill="#EA4335" d="M12 5.37c1.62 0 3.06.56 4.21 1.64l3.15-3.15C17.45 2.09 14.97 1 12 1 7.7 1 3.99 3.47 2.18 7.06L5.84 9.9C6.71 7.3 9.14 5.37 12 5.37z" />
    </svg>
  );
}

function GitHubLogo() {
  return (
    <svg className="oauth-brand-icon github-brand-icon" viewBox="0 0 24 24" aria-hidden="true">
      <path fill="currentColor" fillRule="evenodd" clipRule="evenodd" d="M12 .5C5.65.5.5 5.65.5 12c0 5.09 3.29 9.4 7.86 10.93.58.1.79-.25.79-.56v-2.14c-3.2.7-3.87-1.37-3.87-1.37-.52-1.33-1.28-1.68-1.28-1.68-1.05-.72.08-.7.08-.7 1.16.08 1.77 1.19 1.77 1.19 1.03 1.76 2.7 1.25 3.36.96.1-.75.4-1.25.73-1.54-2.55-.29-5.23-1.28-5.23-5.68 0-1.25.45-2.28 1.18-3.08-.12-.29-.51-1.46.11-3.04 0 0 .97-.31 3.16 1.18A10.9 10.9 0 0 1 12 6.08c.98 0 1.95.13 2.87.39 2.19-1.49 3.15-1.18 3.15-1.18.63 1.58.24 2.75.12 3.04.74.8 1.18 1.83 1.18 3.08 0 4.41-2.69 5.38-5.25 5.67.42.36.79 1.07.79 2.16v3.2c0 .31.21.67.8.56A11.5 11.5 0 0 0 23.5 12C23.5 5.65 18.35.5 12 .5z" />
    </svg>
  );
}

function FacebookLogo() {
  return (
    <svg className="oauth-brand-icon" viewBox="0 0 24 24" aria-hidden="true">
      <path fill="#1877F2" d="M24 12.07C24 5.43 18.63.05 12 .05S0 5.43 0 12.07c0 6 4.39 10.98 10.13 11.88v-8.4H7.08v-3.48h3.05V9.42c0-3.02 1.79-4.68 4.53-4.68 1.31 0 2.68.23 2.68.23v2.96h-1.51c-1.49 0-1.96.93-1.96 1.88v2.26h3.33l-.53 3.48h-2.8v8.4C19.61 23.05 24 18.07 24 12.07z" />
      <path fill="#fff" d="m16.67 15.55.53-3.48h-3.33V9.81c0-.95.47-1.88 1.96-1.88h1.51V4.97s-1.37-.23-2.68-.23c-2.74 0-4.53 1.66-4.53 4.68v2.65H7.08v3.48h3.05v8.4a12.1 12.1 0 0 0 3.74 0v-8.4h2.8z" />
    </svg>
  );
}

function LinkedInLogo() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path fill="currentColor" d="M20.45 20.45h-3.56v-5.58c0-1.33-.03-3.04-1.85-3.04-1.85 0-2.13 1.45-2.13 2.94v5.68H9.35V9h3.42v1.56h.05c.48-.9 1.64-1.85 3.37-1.85 3.6 0 4.27 2.37 4.27 5.46v6.28zM5.34 7.43a2.06 2.06 0 1 1 0-4.13 2.06 2.06 0 0 1 0 4.13zm1.78 13.02H3.56V9h3.56v11.45zM22.22 0H1.77C.79 0 0 .77 0 1.72v20.56C0 23.23.79 24 1.77 24h20.45c.98 0 1.78-.77 1.78-1.72V1.72C24 .77 23.2 0 22.22 0z" />
    </svg>
  );
}

function InstagramLogo() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path fill="currentColor" d="M7.8 2h8.4A5.81 5.81 0 0 1 22 7.8v8.4a5.81 5.81 0 0 1-5.8 5.8H7.8A5.81 5.81 0 0 1 2 16.2V7.8A5.81 5.81 0 0 1 7.8 2zm-.2 2A3.6 3.6 0 0 0 4 7.6v8.8A3.6 3.6 0 0 0 7.6 20h8.8a3.6 3.6 0 0 0 3.6-3.6V7.6A3.6 3.6 0 0 0 16.4 4H7.6zm9.65 1.5a1.25 1.25 0 1 1 0 2.5 1.25 1.25 0 0 1 0-2.5zM12 7.1a4.9 4.9 0 1 1 0 9.8 4.9 4.9 0 0 1 0-9.8zm0 2a2.9 2.9 0 1 0 0 5.8 2.9 2.9 0 0 0 0-5.8z" />
    </svg>
  );
}

function XLogo() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path fill="currentColor" d="M18.9 2h3.28l-7.16 8.18L23.44 22h-6.6l-5.17-6.76L5.75 22H2.47l7.66-8.75L2.06 2h6.77l4.67 6.18L18.9 2zm-1.15 17.92h1.82L7.84 3.98H5.89l11.86 15.94z" />
    </svg>
  );
}

function readCliLoginRequest() {
  const url = new URL(window.location.href);
  const redirectUri = url.searchParams.get('cli_redirect') || '';
  const state = url.searchParams.get('state') || '';
  if (!redirectUri || !state) return null;
  try {
    const callback = new URL(redirectUri);
    const allowed = callback.protocol === 'http:'
      && ['127.0.0.1', 'localhost'].includes(callback.hostname)
      && callback.pathname === '/callback';
    return allowed ? { redirectUri, state } : null;
  } catch {
    return null;
  }
}

function completeCliLogin(cliLogin, token) {
  if (!cliLogin?.redirectUri || !token) return false;
  const callback = new URL(cliLogin.redirectUri);
  callback.searchParams.set('token', token);
  callback.searchParams.set('state', cliLogin.state);
  window.location.href = callback.toString();
  return true;
}

function withCliParams(url, cliLogin) {
  if (!cliLogin) return url;
  const nextUrl = new URL(url, window.location.origin);
  nextUrl.searchParams.set('cli_redirect', cliLogin.redirectUri);
  nextUrl.searchParams.set('state', cliLogin.state);
  return nextUrl.toString();
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

function PasswordInput(props) {
  const [visible, setVisible] = useState(false);

  return (
    <div className="password-field">
      <Input {...props} type={visible ? 'text' : 'password'} />
      <Button
        className="password-toggle"
        variant="ghost"
        size="sm"
        type="button"
        aria-label={visible ? 'Hide password' : 'Show password'}
        title={visible ? 'Hide password' : 'Show password'}
        onClick={() => setVisible(!visible)}
      >
        {visible ? <EyeOff size={17} /> : <Eye size={17} />}
      </Button>
    </div>
  );
}

function Textarea(props) {
  return <textarea className="input textarea" {...props} />;
}

function Label({ children, className = '' }) {
  return <label className={cn('label', className)}>{children}</label>;
}

function SelectField({ value, onValueChange, options }) {
  return (
    <SelectPrimitive.Root value={value} onValueChange={onValueChange}>
      <SelectPrimitive.Trigger className="select-trigger">
        <SelectPrimitive.Value />
        <SelectPrimitive.Icon className="select-icon">
          <ChevronDown size={16} />
        </SelectPrimitive.Icon>
      </SelectPrimitive.Trigger>
      <SelectPrimitive.Portal>
        <SelectPrimitive.Content className="select-content" position="popper" sideOffset={4}>
          <SelectPrimitive.ScrollUpButton className="select-scroll-button">
            <ChevronUp size={16} />
          </SelectPrimitive.ScrollUpButton>
          <SelectPrimitive.Viewport className="select-viewport">
            {options.map(option => (
              <SelectPrimitive.Item className="select-item" key={option.value} value={option.value}>
                <SelectPrimitive.ItemText>{option.label}</SelectPrimitive.ItemText>
                <SelectPrimitive.ItemIndicator className="select-indicator">
                  <Check size={16} />
                </SelectPrimitive.ItemIndicator>
              </SelectPrimitive.Item>
            ))}
          </SelectPrimitive.Viewport>
          <SelectPrimitive.ScrollDownButton className="select-scroll-button">
            <ChevronDown size={16} />
          </SelectPrimitive.ScrollDownButton>
        </SelectPrimitive.Content>
      </SelectPrimitive.Portal>
    </SelectPrimitive.Root>
  );
}

function FileField({ accept, file, label = 'Choose file', onChange }) {
  return (
    <div className="file-field">
      <input className="file-input" type="file" accept={accept} aria-label={label} onChange={onChange} />
      <span className="file-action">
        <UploadCloud size={16} />
        Choose file
      </span>
      <span className={cn('file-name', file && 'selected')} title={file?.name || ''}>
        {file?.name || 'No file selected'}
      </span>
    </div>
  );
}

function BrandMark() {
  return <img className="brand-logo" src="/navrobotec.svg" alt="Navrobotec" />;
}

function AuthNavigation({ navigate }) {
  function goBack() {
    if (window.history.length > 1) {
      window.history.back();
    } else {
      navigate('home');
    }
  }

  return (
    <div className="auth-navigation">
      <Button variant="outline" size="sm" type="button" onClick={goBack}>
        <ArrowLeft size={16} />
        Back
      </Button>
      <Button variant="outline" size="sm" type="button" onClick={() => navigate('home')}>
        <House size={16} />
        Home
      </Button>
    </div>
  );
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
  const [providers, setProviders] = useState({ email_password: true, google: false, github: false, facebook: false });
  const [stats, setStats] = useState(null);
  const [packages, setPackages] = useState([]);
  const [toolchains, setToolchains] = useState([]);
  const [namespaces, setNamespaces] = useState([]);
  const [members, setMembers] = useState([]);
  const [auditLogs, setAuditLogs] = useState([]);
  const [emailStatus, setEmailStatus] = useState(null);
  const [route, setRoute] = useState(() => normalizeRoute(window.location.pathname));
  const [query, setQuery] = useState('');
  const [toolchainQuery, setToolchainQuery] = useState('');
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
  const [cliLogin] = useState(() => readCliLoginRequest());

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
      const callbackLogin = readCliLoginRequest();
      if (completeCliLogin(callbackLogin, oauthToken)) return;
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
      if (completeCliLogin(cliLogin, data.token)) return;
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
      if (completeCliLogin(cliLogin, data.token)) return;
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
      if (completeCliLogin(cliLogin, data.token)) return;
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
    const value = toolchainQuery.trim().toLowerCase();
    return toolchainGroups.filter(group => {
      if (activeVendor !== 'all' && group.vendor_kind !== activeVendor) return false;
      if (toolchainFilters.os !== 'all' && !group.artifacts.some(artifact => artifact.os === toolchainFilters.os)) return false;
      if (toolchainFilters.arch !== 'all' && !group.artifacts.some(artifact => artifact.arch === toolchainFilters.arch)) return false;
      if (toolchainFilters.board !== 'all' && !group.boards.includes(toolchainFilters.board)) return false;
      if (value) {
        const haystack = `${group.name} ${group.description} ${group.vendor} ${group.version} ${group.boards.join(' ')} ${group.artifacts.map(artifact => `${artifact.os}/${artifact.arch}`).join(' ')}`.toLowerCase();
        if (!haystack.includes(value)) return false;
      }
      return true;
    });
  }, [toolchainGroups, activeVendor, toolchainFilters, toolchainQuery]);

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
    toolchainQuery,
    setToolchainQuery,
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
        cliLogin={cliLogin}
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
      {route === 'privacy' && <LegalPage {...sharedPageProps} slug="privacy" user={user} />}
      {route === 'terms' && <LegalPage {...sharedPageProps} slug="terms" user={user} />}
      {route === 'home' && <HomePage {...sharedPageProps} />}
      {route === 'install' && <InstallPage />}
    </main>
  );
}

function Header({ route, navigate, isAuthed, user, logout, theme, setTheme }) {
  const links = [
    ['home', 'Registry'],
    ['install', 'Install'],
    ['packages', 'Packages'],
    ['toolchains', 'Toolchains']
  ];
  if (isAuthed) links.push(['namespaces', 'Namespaces'], ['settings', 'Settings']);
  if (user?.system_role === 'root') links.push(['admin', 'Admin']);

  return (
    <header className="site-header">
      <button className="brand-button" onClick={() => navigate('home')}>
        <BrandMark />
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

function AuthPage({ mode, form, setForm, providers, error, notice, submitAuth, navigate, cliLogin }) {
  const isSignup = mode === 'signup';

  return (
    <main className="auth-page">
      <AuthNavigation navigate={navigate} />
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <BrandMark />
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
            <PasswordInput value={form.password} onChange={event => setForm({ ...form, password: event.target.value })} />
          </Label>
          <Button type="submit" className="full-button">
            {isSignup ? 'Create account' : 'Log in'}
            <ChevronRight size={16} />
          </Button>
        </form>

        <div className="divider"><span>or continue with</span></div>

        <div className="oauth-row">
          <a className={cn('btn btn-outline btn-default', !providers.google && 'disabled')} href={providers.google ? withCliParams(`${API}/auth/google`, cliLogin) : undefined}>
            <GoogleLogo />
            Google
          </a>
          <a className={cn('btn btn-outline btn-default', !providers.github && 'disabled')} href={providers.github ? withCliParams(`${API}/auth/github`, cliLogin) : undefined}>
            <GitHubLogo />
            GitHub
          </a>
          <a className={cn('btn btn-outline btn-default', !providers.facebook && 'disabled')} href={providers.facebook ? withCliParams(`${API}/auth/facebook`, cliLogin) : undefined}>
            <FacebookLogo />
            Facebook
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
      <AuthNavigation navigate={navigate} />
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <BrandMark />
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
      <AuthNavigation navigate={navigate} />
      <Card className="auth-card">
        <button className="brand-button auth-brand" onClick={() => navigate('home')}>
          <BrandMark />
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
                <PasswordInput value={resetForm.password} onChange={event => setResetForm({ ...resetForm, password: event.target.value })} />
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

function getInstallers() {
  return [
    {
      name: 'Windows',
      logo: '/windows-applications-svgrepo-com.svg',
      command: `irm ${API}/downloads/nav/install.ps1 | iex`,
      detail: 'PowerShell installer for Windows 10 and later.'
    },
    {
      name: 'Linux',
      logo: '/linux-svgrepo-com.svg',
      command: `curl -fsSL ${API}/downloads/nav/install.sh | sh`,
      detail: 'Shell installer for Linux distributions with curl.'
    },
    {
      name: 'macOS',
      logo: '/apple-logo-svgrepo-com.svg',
      command: `curl -fsSL ${API}/downloads/nav/install.sh | sh`,
      detail: 'Shell installer for Apple Silicon and Intel Macs.'
    }
  ];
}

function HomePage({ stats, filteredPackages, filteredToolchainGroups, toolchainFilterOptions, navigate }) {
  const boards = toolchainFilterOptions?.boards?.length || 0;
  const platforms = toolchainFilterOptions?.platforms?.length || 0;
  const packageCount = stats?.packages ?? filteredPackages?.length ?? 0;
  const versionCount = stats?.package_versions ?? 0;
  const toolchainCount = stats?.toolchains ?? filteredToolchainGroups?.length ?? 0;
  const downloads = filteredPackages?.reduce((total, pkg) => total + Number(pkg.total_downloads || pkg.downloads || 0), 0) || 0;
  const metrics = [
    { label: 'Packages', value: packageCount, detail: 'Public firmware modules', icon: Package },
    { label: 'Versions', value: versionCount, detail: 'Immutable releases', icon: Boxes },
    { label: 'Boards', value: boards || '10+', detail: 'Board-aware targets', icon: CircuitBoard },
    { label: 'Toolchains', value: toolchainCount, detail: 'Managed compilers and uploaders', icon: Wrench },
    { label: 'Platforms', value: platforms || 3, detail: 'Windows, Linux, macOS', icon: Monitor },
    { label: 'Downloads', value: downloads, detail: 'Verified archive pulls', icon: Download }
  ];

  const featureRows = [
    {
      title: 'Interactive CLI for firmware work',
      eyebrow: 'From empty folder to board flash',
      copy: 'Nav gives embedded teams the same flow developers expect from modern package managers: set up a project, add modules, build, upload, and monitor from the terminal.',
      bullets: ['nav setup initializes the project', 'nav add resolves packages and tools', 'nav build and nav run use the project target'],
      icon: Activity,
      videoTitle: 'CLI install and build demo',
      videoSrc: '/card%201.mp4'
    },
    {
      title: 'Toolchains managed by the registry',
      eyebrow: 'No manual compiler hunting',
      copy: 'Packages can declare the compiler, uploader, board support, and platform artifacts they need. Nav installs the right version for the user’s OS and verifies it before use.',
      bullets: ['Versioned compiler archives', 'SHA256 verification before extraction', 'OS and architecture aware installs'],
      icon: Wrench,
      videoTitle: 'Toolchain auto-install demo'
    },
    {
      title: 'Reusable embedded modules',
      eyebrow: 'npm-style dependency graph',
      copy: 'Publish HAL modules, board packages, drivers, and firmware helpers once. Projects consume them through Nav and keep dependency metadata in the project manifest.',
      bullets: ['Module packages with ABI metadata', 'Dependency resolution across nested packages', 'Changelogs and immutable versions'],
      icon: Package,
      videoTitle: 'Package add and module demo'
    },
    {
      title: 'Security built into publishing',
      eyebrow: 'Safe before it reaches users',
      copy: 'The registry stores package metadata separately from immutable blobs and validates uploads through workers before they become trusted install targets.',
      bullets: ['Archive limits and checksum validation', 'Worker scan pipeline', 'Future-ready signatures, SBOM, and CVE checks'],
      icon: ShieldCheck,
      videoTitle: 'Publish validation demo'
    }
  ];

  const footerLinks = [
    ['Packages', 'packages'],
    ['Toolchains', 'toolchains'],
    ['Install CLI', 'install'],
    ['Namespaces', 'namespaces'],
    ['Privacy', 'privacy'],
    ['Terms', 'terms']
  ];
  const socialLinks = [
    { label: 'LinkedIn', href: 'https://www.linkedin.com/company/navrobotec/posts/?feedView=all', icon: LinkedInLogo },
    { label: 'GitHub', href: 'https://github.com/ragnar-vallhala/nav', icon: GitHubLogo },
    { label: 'Instagram', href: 'https://www.instagram.com/navrobotec', icon: InstagramLogo },
    { label: 'X', href: 'https://x.com/NAVRobotec', icon: XLogo }
  ];

  return (
    <Page className="home-page">
      <section className="home-hero">
        <div className="home-hero-copy">
          <p className="hero-kicker">Nav Registry</p>
          <h1>npm for embedded systems.</h1>
          <p>
            Install hardware packages, board support, compilers, uploaders, and reusable firmware modules from one registry.
          </p>
          <div className="hero-actions">
            <Button onClick={() => navigate('install')}>Install Nav</Button>
            <Button variant="outline" onClick={() => navigate('packages')}>Browse packages</Button>
            <Button variant="ghost" onClick={() => navigate('toolchains')}>View toolchains</Button>
          </div>
        </div>
        <Card className="hero-terminal">
          <div className="terminal-dots" aria-hidden="true"><span /><span /><span /></div>
          <code>nav setup</code>
          <code>nav add nav/blink-add</code>
          <code>nav build</code>
          <code>nav run</code>
          <p>resolved packages, installed toolchains, built firmware, ready to flash</p>
        </Card>
      </section>

      <section className="home-metrics" aria-label="Registry metrics">
        {metrics.map(metric => {
          const Icon = metric.icon;
          return (
            <Card className="metric-card" key={metric.label}>
              <Icon size={19} />
              <span>{metric.label}</span>
              <strong>{metric.value}</strong>
              <p>{metric.detail}</p>
            </Card>
          );
        })}
      </section>

      <section className="home-feature-stack">
        {featureRows.map((feature, index) => {
          const Icon = feature.icon;
          return (
            <article className="home-feature-row" key={feature.title} style={{ '--stack-index': index }}>
              <div className="feature-copy">
                <div className="feature-icon"><Icon size={20} /></div>
                <p>{feature.eyebrow}</p>
                <h2>{feature.title}</h2>
                <span>{feature.copy}</span>
                <ul>
                  {feature.bullets.map(bullet => <li key={bullet}>{bullet}</li>)}
                </ul>
              </div>
              <Card className="feature-video-card">
                {feature.videoSrc ? (
                  <video
                    className="feature-video"
                    src={feature.videoSrc}
                    preload="auto"
                    autoPlay
                    muted
                    loop
                    playsInline
                    aria-label={feature.videoTitle}
                  />
                ) : (
                  <div className="video-placeholder">
                    <span className="play-mark" aria-hidden="true" />
                    <strong>{feature.videoTitle}</strong>
                    <p>Video slot {index + 1}</p>
                  </div>
                )}
              </Card>
            </article>
          );
        })}
      </section>

      <footer className="home-footer">
        <div className="home-footer-main">
          <div className="footer-brand">
            <div className="footer-brand-title">
              <BrandMark />
              <strong>Nav Registry</strong>
            </div>
            <p>Packages, modules, boards, and managed toolchains for embedded firmware teams.</p>
          </div>
          <div className="footer-columns">
            <div className="footer-column">
              <strong>Registry</strong>
              <nav className="footer-link-grid" aria-label="Footer registry navigation">
                {footerLinks.map(([label, target]) => (
                  <button key={label} onClick={() => navigate(target)}>{label}</button>
                ))}
              </nav>
            </div>
            <div className="footer-column">
              <strong>Social</strong>
              <div className="footer-socials">
                {socialLinks.map(({ label, href, icon: Icon }) => (
                  <a className="social-link" key={label} href={href} target="_blank" rel="noreferrer" aria-label={label}>
                    <Icon />
                  </a>
                ))}
              </div>
            </div>
          </div>
        </div>
        <div className="home-footer-bottom">
          <span>Navrobotec</span>
          <span>Development registry for embedded packages.</span>
        </div>
      </footer>
    </Page>
  );
}

function InstallPage() {
  const installers = getInstallers();

  return (
    <Page className="install-page">
      <section className="install-hero">
        <p className="hero-kicker">Install Nav CLI</p>
        <h1>Start building embedded projects from the terminal.</h1>
        <p>
          Pick your operating system, run the command, then use Nav to install packages, resolve toolchains, build firmware, and upload to boards.
        </p>
      </section>

      <section className="install-grid install-page-grid">
        {installers.map(installer => (
          <Card className="install-card install-page-card" key={installer.name}>
            <div className="install-header">
              <span className="os-logo-frame">
                <img className="os-logo" src={installer.logo} alt="" />
              </span>
              <div>
                <h3>{installer.name}</h3>
                <p>{installer.detail}</p>
              </div>
            </div>
            <CommandLine command={installer.command} />
          </Card>
        ))}
      </section>

      <Card className="install-next-card">
        <h2>After installation</h2>
        <div className="install-next-grid">
          <CommandLine command="nav login" />
          <CommandLine command="nav setup" />
          <CommandLine command="nav build" />
        </div>
      </Card>
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
      <section className="catalog-section">
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
      </section>
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
                <SelectField
                  value={memberForm.role}
                  onValueChange={role => setMemberForm({ ...memberForm, role })}
                  options={[
                    { value: 'maintainer', label: 'Maintainer' },
                    { value: 'admin', label: 'Admin' },
                    { value: 'viewer', label: 'Viewer' }
                  ]}
                />
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
  toolchainQuery,
  setToolchainQuery,
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
  useEffect(() => setPage(1), [activeVendor, toolchainQuery, toolchainFilters.os, toolchainFilters.arch, toolchainFilters.board]);
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
      <section className="catalog-section">
        <div className="section-heading">
          <div>
            <h2>Managed Toolchains</h2>
            <p>Grouped by toolchain version, with platform artifacts and official source links.</p>
          </div>
          <div className="toolbar-actions">
            <div className="search-box">
              <Search size={17} />
              <Input value={toolchainQuery} onChange={event => setToolchainQuery(event.target.value)} placeholder="Search toolchains..." />
            </div>
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
              <SelectField
                value={toolchainFilters.os}
                onValueChange={os => setToolchainFilters({ ...toolchainFilters, os })}
                options={[
                  { value: 'all', label: 'All OS' },
                  ...toolchainFilterOptions.os.map(os => ({ value: os, label: os }))
                ]}
              />
            </Label>
            <Label>
              Architecture
              <SelectField
                value={toolchainFilters.arch}
                onValueChange={arch => setToolchainFilters({ ...toolchainFilters, arch })}
                options={[
                  { value: 'all', label: 'All architectures' },
                  ...toolchainFilterOptions.arch.map(arch => ({ value: arch, label: arch }))
                ]}
              />
            </Label>
            <Label>
              Board family
              <SelectField
                value={toolchainFilters.board}
                onValueChange={board => setToolchainFilters({ ...toolchainFilters, board })}
                options={[
                  { value: 'all', label: 'All boards' },
                  ...toolchainFilterOptions.boards.map(board => ({ value: board, label: board }))
                ]}
              />
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
      </section>
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
              <div className="label">
                <span>Archive blob</span>
                <FileField label="Archive blob" file={publishForm.file} onChange={event => setPublishForm({ ...publishForm, file: event.target.files?.[0] || null })} />
              </div>
              <Label>
                Changelog
                <Textarea
                  rows={5}
                  placeholder="Describe what changed in this version."
                  value={publishForm.changelogText}
                  onChange={event => setPublishForm({ ...publishForm, changelogText: event.target.value })}
                />
              </Label>
              <div className="label">
                <span>Changelog file</span>
                <FileField
                  accept=".md,.txt,text/markdown,text/plain"
                  label="Changelog file"
                  file={publishForm.changelogFile}
                  onChange={event => setPublishForm({ ...publishForm, changelogFile: event.target.files?.[0] || null })}
                />
              </div>
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
            <SelectField
              value={toolchainPublishForm.vendor_kind}
              onValueChange={vendor_kind => setToolchainPublishForm({ ...toolchainPublishForm, vendor_kind })}
              options={[
                { value: 'nav', label: 'Nav' },
                { value: 'official', label: 'Official compiler' },
                { value: 'hardware', label: 'Hardware vendor' }
              ]}
            />
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
            <SelectField
              value={toolchainPublishForm.os}
              onValueChange={os => setToolchainPublishForm({ ...toolchainPublishForm, os })}
              options={[
                { value: 'windows', label: 'Windows' },
                { value: 'linux', label: 'Linux' },
                { value: 'darwin', label: 'macOS' }
              ]}
            />
          </Label>
          <Label>
            Architecture
            <SelectField
              value={toolchainPublishForm.arch}
              onValueChange={arch => setToolchainPublishForm({ ...toolchainPublishForm, arch })}
              options={[
                { value: 'x64', label: 'x64' },
                { value: 'arm64', label: 'arm64' }
              ]}
            />
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
          <div className="label">
            <span>Archive</span>
            <FileField label="Archive" file={toolchainPublishForm.file} onChange={event => setToolchainPublishForm({ ...toolchainPublishForm, file: event.target.files?.[0] || null })} />
          </div>
          <Button type="submit">Publish toolchain</Button>
        </form>
      </Card>
    </Page>
  );
}

function LegalPage({ slug, request, user }) {
  const [document, setDocument] = useState(null);
  const [draft, setDraft] = useState({ title: '', body: '', effective_date: '' });
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [editOpen, setEditOpen] = useState(false);
  const canEdit = user?.system_role === 'root';

  useEffect(() => {
    let alive = true;
    setError('');
    setNotice('');
    request(`/legal/${slug}`)
      .then(data => {
        if (!alive) return;
        const doc = data.document;
        setDocument(doc);
        setDraft({
          title: doc.title || '',
          body: doc.body || '',
          effective_date: String(doc.effective_date || '').slice(0, 10)
        });
      })
      .catch(err => {
        if (alive) setError(err.message);
      });
    return () => {
      alive = false;
    };
  }, [slug]);

  async function saveLegalDocument(event) {
    event.preventDefault();
    setError('');
    setNotice('');
    try {
      const data = await request(`/legal/${slug}`, {
        method: 'PUT',
        body: JSON.stringify(draft)
      });
      setDocument(data.document);
      setDraft({
        title: data.document.title || '',
        body: data.document.body || '',
        effective_date: String(data.document.effective_date || '').slice(0, 10)
      });
      setEditOpen(false);
      setNotice(`${data.document.title} updated.`);
    } catch (err) {
      setError(err.message);
    }
  }

  return (
    <Page
      title={document?.title || (slug === 'privacy' ? 'Privacy Policy' : 'Terms of Service')}
      description={document ? `Effective ${formatDate(document.effective_date)} - updated ${formatDate(document.updated_at)}` : ''}
      actions={canEdit ? (
        <Button variant="outline" size="sm" onClick={() => setEditOpen(true)}>
          <Pencil size={15} />
          Edit
        </Button>
      ) : null}
    >
      <PageAlerts error={error} notice={notice} />
      <div className="legal-layout">
        <Card className="section-card legal-document">
          {renderLegalBody(document?.body || 'Loading document...')}
        </Card>
        {canEdit && editOpen && (
          <div className="modal-backdrop" role="presentation" onMouseDown={() => setEditOpen(false)}>
            <Card className="section-card legal-editor modal-card" role="dialog" aria-modal="true" aria-label={`Edit ${document?.title || 'document'}`} onMouseDown={event => event.stopPropagation()}>
              <div className="modal-heading">
                <div>
                  <h2>Edit document</h2>
                  <p>{document?.title || 'Legal document'}</p>
                </div>
                <Button variant="ghost" size="sm" type="button" onClick={() => setEditOpen(false)}>
                  <X size={16} />
                </Button>
              </div>
              <form className="stack-form" onSubmit={saveLegalDocument}>
                <Label>
                  Title
                  <Input value={draft.title} onChange={event => setDraft({ ...draft, title: event.target.value })} />
                </Label>
                <Label>
                  Effective date
                  <Input type="date" value={draft.effective_date} onChange={event => setDraft({ ...draft, effective_date: event.target.value })} />
                </Label>
                <Label>
                  Body
                  <Textarea value={draft.body} onChange={event => setDraft({ ...draft, body: event.target.value })} rows={16} />
                </Label>
                <div className="button-row">
                  <Button type="submit">Save document</Button>
                  <Button type="button" variant="outline" onClick={() => setEditOpen(false)}>Cancel</Button>
                </div>
              </form>
            </Card>
          </div>
        )}
      </div>
    </Page>
  );
}

function Page({ title, description, actions = null, children, className = '' }) {
  return (
    <section className={cn('page', className)}>
      {title && (
        <header className="page-header">
          <div>
            <h1>{title}</h1>
            {description && <p>{description}</p>}
          </div>
          {actions && <div className="page-actions">{actions}</div>}
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
            <SelectField
              value={memberForm.role}
              onValueChange={role => setMemberForm({ ...memberForm, role })}
              options={[
                { value: 'maintainer', label: 'Maintainer' },
                { value: 'admin', label: 'Admin' },
                { value: 'viewer', label: 'Viewer' }
              ]}
            />
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
          <div className="label">
            <span>Archive blob</span>
            <FileField label="Archive blob" file={publishForm.file} onChange={event => setPublishForm({ ...publishForm, file: event.target.files?.[0] || null })} />
          </div>
          <Label>
            Changelog
            <Textarea
              rows={4}
              placeholder="What changed in this version?"
              value={publishForm.changelogText}
              onChange={event => setPublishForm({ ...publishForm, changelogText: event.target.value })}
            />
          </Label>
          <div className="label">
            <span>Changelog file</span>
            <FileField
              accept=".md,.txt,text/markdown,text/plain"
              label="Changelog file"
              file={publishForm.changelogFile}
              onChange={event => setPublishForm({ ...publishForm, changelogFile: event.target.files?.[0] || null })}
            />
          </div>
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
  if (pathname.includes('privacy')) return 'privacy';
  if (pathname.includes('terms')) return 'terms';
  if (pathname.includes('verify-email')) return 'verify-email';
  if (pathname.includes('forgot-password')) return 'forgot-password';
  if (pathname.includes('reset-password')) return 'reset-password';
  if (pathname.includes('invite')) return 'invite';
  if (pathname === '/settings') return 'settings';
  if (pathname.includes('install')) return 'install';
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
  if (pathname.includes('cli')) return 'install';
  if (pathname.includes('security')) return 'toolchains';
  return 'home';
}

function routeToPath(route) {
  if (route === 'home') return '/';
  if (route === 'install') return '/install';
  if (route === 'privacy') return '/privacy';
  if (route === 'terms') return '/terms';
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

function formatDate(value) {
  if (!value) return 'not set';
  return new Date(value).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
}

function renderLegalBody(body) {
  const blocks = String(body || '').split(/\n{2,}/).filter(Boolean);
  return blocks.map((block, index) => {
    if (block.startsWith('# ')) return <h2 key={index}>{block.replace(/^#\s+/, '')}</h2>;
    if (block.startsWith('## ')) return <h3 key={index}>{block.replace(/^##\s+/, '')}</h3>;
    const lines = block.split('\n').filter(Boolean);
    if (lines.every(line => line.trim().startsWith('- '))) {
      return (
        <ul key={index}>
          {lines.map(line => <li key={line}>{line.trim().replace(/^-\s+/, '')}</li>)}
        </ul>
      );
    }
    return <p key={index}>{lines.join(' ')}</p>;
  });
}

async function sha256Hex(arrayBuffer) {
  const hash = await crypto.subtle.digest('SHA-256', arrayBuffer);
  return [...new Uint8Array(hash)].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

createRoot(document.getElementById('root')).render(<App />);
