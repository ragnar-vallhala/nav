"use client";

import React, { useState, useEffect, useRef } from 'react';
import ReactMarkdown from 'react-markdown';
import { 
  Compass, LayoutGrid, Package, FolderGit, Settings, 
  User, Activity, Bell, Search, Download, Shield,
  Bold, Italic, Link, Code, List, Image as ImageIcon, Share2, CornerDownRight, ArrowLeft
} from 'lucide-react';
import NextLink from 'next/link';

export default function Home({ defaultContext = 'registry', activePostId }: { defaultContext?: 'registry' | 'docs' | 'community', activePostId?: string }) {
  const [query, setQuery] = useState('');
  const [results, setResults] = useState<any[]>([]);
  const [searching, setSearching] = useState(false);
  
  // Multi-Workspace Context & Layout Routing
  const [appContext, setAppContext] = useState(defaultContext);
  const [activeTab, setActiveTab] = useState<string>(
    defaultContext === 'community' ? 'community' : (defaultContext === 'docs' ? 'docs' : 'dashboard')
  );
  
  // Dynamic Identity Persistence
  const [sessionToken, setSessionToken] = useState<string | null>(null);
  const [showAuth, setShowAuth] = useState(false);
  const [isLoginMode, setIsLoginMode] = useState(true);
  const [needsVerification, setNeedsVerification] = useState(false);
  
  const [authUsername, setAuthUsername] = useState('');
  const [authEmail, setAuthEmail] = useState('');
  const [authPassword, setAuthPassword] = useState('');
  const [verificationCode, setVerificationCode] = useState('');

  // Token Silo Operations
  const [userTokens, setUserTokens] = useState<any[]>([]);
  const [newTokenName, setNewTokenName] = useState('');

  // Interactive Social Matrix State
  const [posts, setPosts] = useState<any[]>([]);
  const [newPostTitle, setNewPostTitle] = useState('');
  const [newPostContent, setNewPostContent] = useState('');
  
  // Nested Discussion System State
  const [openComments, setOpenComments] = useState<string | null>(null);
  const [commentsCache, setCommentsCache] = useState<Record<string, any[]>>({});
  const [newCommentBody, setNewCommentBody] = useState('');
  const [replyTo, setReplyTo] = useState<string | null>(null);
  const [singlePost, setSinglePost] = useState<any | null>(null);
  const [toast, setToast] = useState<string | null>(null);

  const composerRef = useRef<HTMLTextAreaElement>(null);

  const triggerNotice = (msg: string) => {
    setToast(msg);
    setTimeout(() => setToast(null), 4500);
  };

  const fetchPosts = async () => {
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const headers: HeadersInit = {};
      if (sessionToken) { headers['Authorization'] = `Bearer ${sessionToken}`; }
      
      const response = await fetch(`${apiHost}/api/v1/community/posts`, { headers });
      if (response.ok) {
        const data = await response.json();
        setPosts(data);
      }
    } catch (error) { console.error("Grid disconnect:", error); }
  };

  const fetchSinglePost = async (id: string) => {
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const headers: HeadersInit = {};
      if (sessionToken) { headers['Authorization'] = `Bearer ${sessionToken}`; }

      const response = await fetch(`${apiHost}/api/v1/community/posts/${id}`, { headers });
      if (response.ok) {
        const data = await response.json();
        setSinglePost(data);
        // Fire forced comments toggle for visual instantiation
        toggleComments(id);
      }
    } catch (e) {}
  };

  const handleCreatePost = async () => {
    if (!sessionToken) { openAuth(true); return; }
    if (!newPostTitle.trim()) return;
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const response = await fetch(`${apiHost}/api/v1/community/posts`, {
        method: 'POST',
        headers: { 
          'Authorization': `Bearer ${sessionToken}`,
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ title: newPostTitle, content: newPostContent })
      });
      if (response.ok) {
        setNewPostTitle('');
        setNewPostContent('');
        fetchPosts();
      }
    } catch (e) {}
  };

  const handleImageUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    if (!e.target.files || !e.target.files[0]) return;
    const file = e.target.files[0];
    const formData = new FormData();
    formData.append('file', file);
    
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const response = await fetch(`${apiHost}/api/v1/community/upload`, {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${sessionToken}` },
        body: formData
      });
      if (response.ok) {
        const data = await response.json();
        // Splice Markdown Injection effortlessly
        setNewPostContent(prev => prev + `\n![image](${data.url})\n`);
      }
    } catch(err) { console.error("Asset push fault:", err); }
  };

  const handleVote = async (postId: string, delta: number) => {
    if (!sessionToken) { openAuth(true); return; }
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      await fetch(`${apiHost}/api/v1/community/posts/${postId}/vote`, {
        method: 'POST',
        headers: { 
          'Authorization': `Bearer ${sessionToken}`,
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ delta })
      });
      if (activePostId) {
        fetchSinglePost(postId);
      } else {
        fetchPosts(); // Instant lattice reconciliation
      }
    } catch (e) {}
  };

  const insertMarkdown = (prefix: string, suffix: string = prefix) => {
    if (!composerRef.current) return;
    const start = composerRef.current.selectionStart;
    const end = composerRef.current.selectionEnd;
    const text = newPostContent;
    const before = text.substring(0, start);
    const selection = text.substring(start, end);
    const after = text.substring(end);

    setNewPostContent(`${before}${prefix}${selection}${suffix}${after}`);
    setTimeout(() => {
      composerRef.current?.focus();
    }, 10);
  };

  const toggleComments = async (postId: string) => {
    if (openComments === postId) {
      setOpenComments(null);
      return;
    }
    setOpenComments(postId);
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const res = await fetch(`${apiHost}/api/v1/community/posts/${postId}/comments`);
      if (res.ok) {
        const list = await res.json();
        setCommentsCache(prev => ({ ...prev, [postId]: list }));
      }
    } catch(e){}
  };

  const submitComment = async (postId: string) => {
    if (!sessionToken) { openAuth(true); return; }
    if (!newCommentBody.trim()) return;
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      const response = await fetch(`${apiHost}/api/v1/community/posts/${postId}/comments`, {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${sessionToken}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ content: newCommentBody, parent_id: replyTo })
      });
      if (response.ok) {
        setNewCommentBody('');
        setReplyTo(null); // Break recursion loop
        // Reload just this post's cache
        const res = await fetch(`${apiHost}/api/v1/community/posts/${postId}/comments`);
        const list = await res.json();
        setCommentsCache(prev => ({ ...prev, [postId]: list }));
        fetchPosts(); // Refreshes comment counters overall
      }
    } catch(e) {}
  };

  const handleReport = () => {
    triggerNotice("> VIOLATION VECTOR DETECTED.\nNode reported for moderator evaluation.");
  };

  const handleShare = (id: string) => {
    const link = `${window.location.origin}/community/post/${id}`;
    navigator.clipboard.writeText(link);
    triggerNotice("> ADDRESS COPIED TO CLIPBOARD.");
  };

  // Dynamic Tree Constructor
  const renderTreeComments = (postId: string, parentId: string | null = null, depth = 0) => {
    const pool = commentsCache[postId] || [];
    const level = pool.filter((c: any) => c.parent_id === parentId);

    return level.map((comment: any) => (
      <div key={comment.id} style={{ marginLeft: depth > 0 ? '20px' : '0', marginTop: '10px' }}>
        <div style={{ background: '#0d1117', border: '1px solid #21262d', padding: '10px', borderRadius: '6px', borderLeft: depth > 0 ? '2px solid #30363d' : '1px solid #21262d' }}>
          <div style={{ fontSize: '11px', color: '#8b949e', marginBottom: '6px', display: 'flex', justifyContent: 'space-between' }}>
            <span>
              <span style={{ color: '#58a6ff', fontWeight: '600' }}>u/{comment.username}</span> 
              • {new Date(comment.created_at).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'})}
            </span>
            <button onClick={() => setReplyTo(replyTo === comment.id ? null : comment.id)} style={{ background: 'none', border: 'none', color: '#58a6ff', fontSize: '11px', cursor: 'pointer', padding: 0 }}>
              Reply
            </button>
          </div>
          <div style={{ fontSize: '13px', color: '#c9d1d9', whiteSpace: 'pre-wrap' }}>{comment.content}</div>
          
          {replyTo === comment.id && (
             <div style={{ marginTop: '8px', display: 'flex', gap: '8px' }}>
                <input 
                   type="text" 
                   placeholder="Deploy recursive echo..." 
                   style={{ flex: 1, background: '#161b22', border: '1px solid #30363d', color: '#fff', fontSize: '12px', padding: '4px 8px', borderRadius: '4px' }}
                   value={newCommentBody}
                   onChange={(e) => setNewCommentBody(e.target.value)}
                />
                <button onClick={() => submitComment(postId)} style={{ background: '#238636', color: '#fff', border: 'none', padding: '0 12px', borderRadius: '4px', fontSize: '11px', cursor: 'pointer' }}>Post</button>
             </div>
          )}
        </div>
        {/* Loop recursion */}
        {renderTreeComments(postId, comment.id, depth + 1)}
      </div>
    ));
  };

  // Check for existing cached uplink on initialization
  useEffect(() => {
    const cached = localStorage.getItem('nav_session');
    if (cached) setSessionToken(cached);
    
    if (activePostId) {
      fetchSinglePost(activePostId);
    } else if (defaultContext === 'community') {
      fetchPosts();
    } else {
      handleSearch();
    }
  }, [activePostId, defaultContext, sessionToken]);

  const handleSearch = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    
    setSearching(true);
    try {
      const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
      // The refactored search accepts empty query implicitly via '%'.
      const response = await fetch(`${apiHost}/api/v1/search?q=${encodeURIComponent(query)}`);
      const data = await response.json();
      if (response.ok) {
        setResults(data.results || []);
      }
    } catch (error) {
      console.error("Backend disconnected.");
      setResults([]);
    } finally { setSearching(false); }
  };

  const openAuth = (login: boolean) => {
    setNeedsVerification(false);
    setIsLoginMode(login);
    setShowAuth(true);
  };

  const handleAuthSubmit = async () => {
    const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
    const endpoint = isLoginMode ? '/api/v1/auth/login' : '/api/v1/auth/register';
    try {
      const response = await fetch(`${apiHost}${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: authUsername, email: authEmail, password: authPassword })
      });
      const data = await response.json();
      
      if (data.status === 'requires_verification') {
        setNeedsVerification(true);
        return;
      }

      if (response.ok) {
        const rawToken = data.token;
        localStorage.setItem('nav_session', rawToken);
        setSessionToken(rawToken);
        setShowAuth(false);
        triggerNotice(`> UPLINK SECURED.\nAccess granted.`);
      } else { triggerNotice(`> ERROR: ${data.error}`); }
    } catch (err) { triggerNotice('> FAILURE: Vector unreachable.'); }
  };

  const handleVerifySubmit = async () => {
    const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
    try {
      const response = await fetch(`${apiHost}/api/v1/auth/verify`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: authUsername, code: verificationCode })
      });
      const data = await response.json();
      
      if (response.ok) {
        localStorage.setItem('nav_session', data.token);
        setSessionToken(data.token);
        setNeedsVerification(false);
        setShowAuth(false);
        triggerNotice(`> VERIFICATION COMPLETE.\nGrid fully operational.`);
      } else {
        triggerNotice(`> ERROR: ${data.error || 'Invalid challenge vector.'}`);
      }
    } catch (err) {
      triggerNotice("> Critical validation link timeout.");
    }
  };

  const handleLogout = () => {
    localStorage.removeItem('nav_session');
    setSessionToken(null);
    setActiveTab('dashboard');
  };

  // --- TOKEN CONTROL OPERATIONS ---
  const fetchTokens = async (tokenOverride?: string) => {
    const targetToken = tokenOverride || sessionToken;
    if (!targetToken) return;

    const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
    try {
      const response = await fetch(`${apiHost}/api/v1/tokens`, {
        headers: { 'Authorization': `Bearer ${targetToken}` }
      });
      const data = await response.json();
      if (response.ok) {
        setUserTokens(data.tokens || []);
      }
    } catch (err) { console.error("Failed token index fetch."); }
  };

  const handleCreateNewToken = async () => {
    if (!sessionToken) return;
    const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
    try {
      const response = await fetch(`${apiHost}/api/v1/tokens`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${sessionToken}` },
        body: JSON.stringify({ name: newTokenName })
      });
      const data = await response.json();
      if (response.ok) {
        triggerNotice(`> NEW VECTOR PROVISIONED\n${data.token}`);
        setNewTokenName('');
        fetchTokens();
      }
    } catch (err) { triggerNotice("> Extraction failed."); }
  };

  const handleRevokeToken = async (id: string) => {
    if (!sessionToken) return;

    const apiHost = window.location.hostname === 'localhost' ? 'http://localhost:8081' : '';
    try {
      const response = await fetch(`${apiHost}/api/v1/tokens/${id}`, {
        method: 'DELETE',
        headers: { 'Authorization': `Bearer ${sessionToken}` }
      });
      if (response.ok) {
        fetchTokens();
        triggerNotice("> VECTOR DECOUPLED.");
      }
    } catch (err) { triggerNotice("> Decouple vector failed."); }
  };

  const navToSettings = () => {
    if (!sessionToken) {
      openAuth(true);
      return;
    }
    setActiveTab('settings');
    fetchTokens();
  };

  return (
    <div className="app-container">
      {/* Left Dynamic Sidebar */}
      <aside className="sidebar">
        <div className="logo-section">
          <Compass className="logo-icon" size={24} />
          <span className="logo-text">Nav <span className="accent-text">Registry</span></span>
        </div>
        
        <nav className="nav-menu">
          {appContext === 'registry' ? (
            <>
              <NextLink href="/" className={`nav-item ${activeTab === 'dashboard' ? 'active' : ''}`}>
                <LayoutGrid size={18} /> <span>Dashboard</span>
              </NextLink>
              <a href="#" className={`nav-item ${activeTab === 'my-packages' ? 'active' : ''}`} 
                 onClick={(e) => { 
                   e.preventDefault(); 
                   if (!sessionToken) { openAuth(true); return; }
                   setActiveTab('my-packages'); 
                 }}>
                <Package size={18} /> <span>My Packages</span>
              </a>
              <a href="#" className={`nav-item ${activeTab === 'settings' ? 'active' : ''}`} onClick={navToSettings}>
                <Settings size={18} /> <span>Settings</span>
              </a>
            </>
          ) : appContext === 'docs' ? (
            <>
              <NextLink href="/docs" className={`nav-item ${activeTab === 'docs' ? 'active' : ''}`}>
                <Compass size={18} /> <span>Quickstart</span>
              </NextLink>
              <a href="#" className="nav-item">
                <FolderGit size={18} /> <span>API Guide</span>
              </a>
              <a href="#" className="nav-item">
                <Shield size={18} /> <span>Best Practices</span>
              </a>
            </>
          ) : (
            <>
              <NextLink href="/community" className={`nav-item ${activeTab === 'community' ? 'active' : ''}`}>
                <Activity size={18} /> <span>All Boards</span>
              </NextLink>
              <a href="#" className="nav-item">
                <User size={18} /> <span>Operators</span>
              </a>
              <a href="#" className="nav-item">
                <Bell size={18} /> <span>Trends</span>
              </a>
            </>
          )}
        </nav>
      </aside>

      {/* Main Dynamic Content Core */}
      <main className="content-area">
        <header className="top-nav">
          <div className="top-links">
            <NextLink href="/" className={`top-link ${appContext === 'registry' ? 'active' : ''}`}>Registry</NextLink>
            <NextLink href="/docs" className={`top-link ${appContext === 'docs' ? 'active' : ''}`}>Docs</NextLink>
            <NextLink href="/community" className={`top-link ${appContext === 'community' ? 'active' : ''}`}>Community</NextLink>
          </div>
          
          <div className="header-actions" style={{ gap: '12px' }}>
            {sessionToken ? (
              <>
                <div className="logged-in-badge">
                  <User size={14} /> <span>ONLINE</span>
                </div>
                <button className="secondary-btn" onClick={handleLogout}>Disconnect</button>
              </>
            ) : (
              <>
                <button className="secondary-btn" onClick={() => openAuth(true)}>Login</button>
                <button className="secondary-btn" onClick={() => openAuth(false)}>Provision</button>
              </>
            )}
          </div>
        </header>

        <div className="scene">
          {activePostId && singlePost ? (
             /* ==============================================
                SINGLE POST VIEW VECTOR
                ============================================== */
             <div style={{ maxWidth: '800px', margin: '0 auto', paddingTop: '20px', paddingBottom: '60px' }}>
                <NextLink href="/community" style={{ display: 'inline-flex', alignItems: 'center', gap: '8px', color: '#58a6ff', textDecoration: 'none', marginBottom: '24px', fontSize: '14px', fontWeight: '500' }}>
                  <ArrowLeft size={16} /> Return to Narrative Stream
                </NextLink>
                
                <div key={singlePost.id} style={{ background: '#161b22', border: '1px solid #30363d', borderRadius: '6px', display: 'flex' }}>
                    {/* Copy same post layout block here */}
                    <div style={{ background: '#0d1117', padding: '12px 8px', display: 'flex', flexDirection: 'column', alignItems: 'center', width: '44px', borderTopLeftRadius: '6px', borderBottomLeftRadius: '6px' }}>
                      <div style={{ cursor: 'pointer', color: singlePost.user_vote === 1 ? '#39d353' : '#8b949e', fontSize: '16px', transition: 'color 0.2s' }} onClick={() => handleVote(singlePost.id, 1)}>▲</div>
                      <div style={{ color: singlePost.user_vote === 1 ? '#39d353' : '#fff', fontWeight: 'bold', fontSize: '12px', margin: '2px 0' }}>{singlePost.upvotes}</div>
                      <div style={{ height: '1px', width: '16px', background: '#30363d', margin: '4px 0' }} />
                      <div style={{ color: singlePost.user_vote === -1 ? '#f85149' : '#fff', fontWeight: 'bold', fontSize: '12px', margin: '2px 0' }}>{singlePost.downvotes}</div>
                      <div style={{ cursor: 'pointer', color: singlePost.user_vote === -1 ? '#f85149' : '#8b949e', fontSize: '16px', transition: 'color 0.2s' }} onClick={() => handleVote(singlePost.id, -1)}>▼</div>
                    </div>
                    <div style={{ flex: 1, padding: '20px' }}>
                      <div style={{ fontSize: '12px', color: '#8b949e', marginBottom: '8px', display: 'flex', alignItems: 'center', gap: '6px' }}>
                        <User size={12} /> <span style={{ color: '#58a6ff', fontWeight: '600' }}>u/{singlePost.username}</span> • <span>{new Date(singlePost.created_at).toLocaleDateString()}</span>
                      </div>
                      <h1 style={{ fontSize: '24px', color: '#fff', marginBottom: '16px', fontWeight: '600', lineHeight: '1.3' }}>{singlePost.title}</h1>
                      <div className="pro-markdown-renderer" style={{ marginBottom: '20px' }}>
                        <ReactMarkdown components={{ img: ({node, ...props}) => <img {...props} style={{ maxWidth: '100%', maxHeight: '600px', border: '1px solid #30363d', borderRadius: '6px', margin: '12px 0', display: 'block' }} /> }}>
                          {singlePost.content || ''}
                        </ReactMarkdown>
                      </div>
                      <div style={{ display: 'flex', gap: '16px', fontSize: '12px', color: '#8b949e', borderTop: '1px solid #30363d', paddingTop: '12px' }}>
                        <button onClick={() => handleShare(singlePost.id)} style={{ background: 'none', border: 'none', color: '#8b949e', display: 'flex', alignItems: 'center', gap: '4px', cursor: 'pointer', padding: 0 }}>
                          <Share2 size={14} /> Share
                        </button>
                        <span onClick={handleReport} style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '4px' }}><Shield size={14} /> Report</span>
                      </div>
                      
                      <div style={{ marginTop: '24px', borderTop: '1px dashed #30363d', paddingTop: '20px' }}>
                         <h3 style={{ color: '#fff', marginBottom: '16px', fontSize: '14px' }}>Echo Responses ({singlePost.comments})</h3>
                         <div style={{ display: 'flex', gap: '10px', marginBottom: '24px' }}>
                            <textarea 
                               placeholder="Transmit clarification..."
                               style={{ flex: 1, background: '#0d1117', border: '1px solid #30363d', color: '#c9d1d9', padding: '10px', fontSize: '13px', minHeight: '80px', borderRadius: '6px' }}
                               value={replyTo === null ? newCommentBody : ''}
                               onChange={(e) => { setReplyTo(null); setNewCommentBody(e.target.value); }}
                            />
                            <button onClick={() => submitComment(singlePost.id)} style={{ alignSelf: 'flex-end', background: '#238636', border: 'none', color: '#fff', padding: '10px 20px', borderRadius: '6px', cursor: 'pointer', fontWeight: 'bold' }}>Send Echo</button>
                         </div>
                         <div>
                            {renderTreeComments(singlePost.id)}
                            {(commentsCache[singlePost.id] || []).length === 0 && <div style={{color:'#8b949e', fontStyle:'italic', fontSize:'13px'}}>Quiet channel detected.</div>}
                         </div>
                      </div>
                    </div>
                </div>
             </div>
          ) : activeTab === 'dashboard' ? (
            /* ==============================================
               DASHBOARD VIEW (Main Search Grid)
               ============================================== */
            <>
              <section className="hero-spotlight">
                <div className="search-matrix-container">
                  <form onSubmit={handleSearch} className="search-bar-glow">
                    <Search className="search-icon" size={20} />
                    <input 
                      type="text" placeholder="Query global index..." 
                      autoComplete="off" id="main-search-input"
                      value={query} onChange={(e) => setQuery(e.target.value)}
                    />
                    <button type="submit" className="search-action-btn">Execute</button>
                  </form>
                </div>
              </section>

              <section className="package-grid-section">
                <div className="section-header">
                  <h2> ACTIVE_INDICES</h2>
                </div>
                <div className="package-grid">
                  {searching ? (
                    <div className="loading-shimmer">Scanning matrix stream...</div>
                  ) : results.length > 0 ? (
                    results.map((pkg, idx) => (
                      <div key={idx} className="package-card">
                        <div className="card-head">
                          <div>
                            <div style={{ fontSize: '12px', color: '#8b949e', marginBottom: '2px' }}>{pkg.namespace} /</div>
                            <h3 className="pkg-title">{pkg.name}</h3>
                          </div>
                          <span className="version-badge">{pkg.version}</span>
                        </div>
                        <p className="pkg-desc">{pkg.description}</p>
                        <div className="card-footer">
                          <div className="metric-cluster">
                            <span><Download size={14} /> {pkg.downloads}</span>
                            <span><Shield size={14} /> Signed</span>
                          </div>
                          <a href="#" className="view-btn">inspect</a>
                        </div>
                      </div>
                    ))
                  ) : (
                    <div style={{ 
                      gridColumn: '1 / -1', padding: '60px 20px', textAlign: 'center', 
                      border: '1px dashed #30363d', background: '#090c10' 
                    }}>
                      <div style={{ fontSize: '40px', color: '#30363d', marginBottom: '12px' }}>∅</div>
                      <h3 style={{ color: '#c9d1d9', marginBottom: '4px' }}>Universal Cold Index Detected</h3>
                      <p style={{ color: '#8b949e', fontSize: '13px' }}>
                        No distributions have broken cover yet. Install the binary toolkit and fire your first upload into the global cluster.
                      </p>
                    </div>
                  )}
                </div>
              </section>
            </>
          ) : activeTab === 'my-packages' ? (
            /* ==============================================
               MY PACKAGES VIEW
               ============================================== */
            <div className="settings-view">
              <div className="page-header">
                <h1>My Active Inventory</h1>
                <div className="page-subtitle">Registered assets explicitly managed within your secure namespace cluster.</div>
              </div>

              <div className="token-catalog" style={{ padding: '60px 20px', textAlign: 'center', background: '#161b22' }}>
                 <Package size={48} style={{ color: '#30363d', marginBottom: '16px' }} />
                 <div style={{ fontSize: '16px', color: '#8b949e', fontWeight: '600' }}>No modules discovered yet</div>
                 <p style={{ color: '#8b949e', fontSize: '13px', marginTop: '8px' }}>Authenticate via CLI and execute `nav publish` to synchronize new distribution.</p>
              </div>
            </div>
          ) : activeTab === 'docs' ? (
            /* ==============================================
               DOCS VIEW
               ============================================== */
            <div style={{ maxWidth: '900px', margin: '0 auto', paddingTop: '20px' }}>
              <div className="page-header">
                <h1 style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
                  <Compass size={32} className="accent-text" /> Nav Registry Documentation
                </h1>
                <div className="page-subtitle">Complete integration guide for global package distribution network.</div>
              </div>
              <div style={{ background: '#161b22', border: '1px solid #30363d', padding: '32px', lineHeight: '1.6' }}>
                <h2 style={{ color: '#fff', fontSize: '22px', marginBottom: '16px' }}>Getting Started</h2>
                <p style={{ color: '#8b949e', marginBottom: '24px' }}>
                  Welcome to the authoritative repository cluster. To begin injecting distributions or installing modules, ensure you have the binary client fully synthesized on your host.
                </p>
                
                <h3 style={{ color: '#39d353', fontSize: '16px', marginBottom: '12px', textTransform: 'uppercase' }}># Synthesis Protocol</h3>
                <div style={{ background: '#0d1117', padding: '16px', borderLeft: '4px solid #58a6ff', fontFamily: 'monospace', color: '#c9d1d9', marginBottom: '24px' }}>
                  curl -sSL https://navrobotec.io/install.sh | bash
                </div>

                <h3 style={{ color: '#39d353', fontSize: '16px', marginBottom: '12px', textTransform: 'uppercase' }}># Secure Uplink</h3>
                <p style={{ color: '#8b949e', marginBottom: '12px' }}>Generate a cryptographic token from your Settings console and tether the client:</p>
                <div style={{ background: '#0d1117', padding: '16px', borderLeft: '4px solid #58a6ff', fontFamily: 'monospace', color: '#c9d1d9' }}>
                  nav auth login --token [YOUR_ACCESS_VECTOR]
                </div>
              </div>
            </div>
          ) : activeTab === 'community' ? (
            /* ==============================================
               COMMUNITY VIEW (Reddit Style)
               ============================================== */
            <div style={{ maxWidth: '800px', margin: '0 auto', paddingTop: '20px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end', marginBottom: '24px', borderBottom: '1px solid #30363d', paddingBottom: '16px' }}>
                <div>
                  <h1 style={{ color: '#fff', letterSpacing: '1px' }}>r/NavRegistry</h1>
                  <div className="page-subtitle">Operators broadcasting live narrative nodes.</div>
                </div>
              </div>

              {/* Broadcast Input Grid - Professional Editor Stack */}
              <div style={{ background: '#161b22', border: '1px solid #30363d', marginBottom: '24px', borderRadius: '6px', overflow: 'hidden' }}>
                <div style={{ background: '#0d1117', borderBottom: '1px solid #30363d', padding: '12px' }}>
                  <input 
                     type="text" 
                     placeholder={sessionToken ? "Vector Title: succinct transmission label..." : "Authentication required to author content..."}
                     disabled={!sessionToken}
                     style={{ width: '100%', background: 'transparent', border: 'none', color: '#fff', fontSize: '16px', fontWeight: 'bold', outline: 'none' }}
                     value={newPostTitle}
                     onChange={(e) => setNewPostTitle(e.target.value)}
                  />
                </div>
                
                {/* Pro Formatting Toolbar */}
                <div style={{ background: '#161b22', borderBottom: '1px solid #30363d', padding: '6px 12px', display: 'flex', gap: '8px', alignItems: 'center' }}>
                  <button onClick={() => insertMarkdown('**')} style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '4px' }} title="Bold"><Bold size={16} /></button>
                  <button onClick={() => insertMarkdown('_')} style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '4px' }} title="Italic"><Italic size={16} /></button>
                  <button onClick={() => insertMarkdown('[text](', ')') } style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '4px' }} title="Link"><Link size={16} /></button>
                  <button onClick={() => insertMarkdown('```\n', '\n```')} style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '4px' }} title="Code"><Code size={16} /></button>
                  <button onClick={() => insertMarkdown('- ')} style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '4px' }} title="Bullet List"><List size={16} /></button>
                  <div style={{ width: '1px', height: '20px', background: '#30363d', margin: '0 4px' }} />
                  <label style={{ cursor: 'pointer', color: '#8b949e', display: 'flex', alignItems: 'center', padding: '4px' }} title="Attach Image">
                    <ImageIcon size={16} />
                    <input type="file" accept="image/*" style={{ display: 'none' }} onChange={handleImageUpload} disabled={!sessionToken} />
                  </label>
                </div>

                <textarea
                   ref={composerRef}
                   placeholder="Support protocols fully optimized: Type standard markdown headers, lists, and code blocks to articulate operational parameters flawlessly."
                   disabled={!sessionToken}
                   style={{ width: '100%', background: '#0d1117', border: 'none', color: '#c9d1d9', padding: '16px', minHeight: '160px', fontFamily: 'ui-monospace, SFMono-Regular, monospace', resize: 'vertical', outline: 'none', lineHeight: '1.6' }}
                   value={newPostContent}
                   onChange={(e) => setNewPostContent(e.target.value)}
                />
                
                <div style={{ background: '#161b22', borderTop: '1px solid #30363d', padding: '10px 16px', display: 'flex', justifyContent: 'flex-end' }}>
                  <button onClick={handleCreatePost} style={{ background: '#238636', color: '#fff', border: '1px solid rgba(240,246,252,0.1)', padding: '6px 16px', fontSize: '14px', fontWeight: '600', cursor: 'pointer', borderRadius: '6px' }}>Release Post</button>
                </div>
              </div>

              <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                {posts.length > 0 ? (
                  posts.map((post) => (
                    <div key={post.id} style={{ background: '#161b22', border: '1px solid #30363d', borderRadius: '6px', display: 'flex', transition: 'border-color 0.2s' }}>
                      {/* Upvote Column */}
                      <div style={{ background: '#0d1117', padding: '12px 8px', display: 'flex', flexDirection: 'column', alignItems: 'center', width: '44px', borderTopLeftRadius: '6px', borderBottomLeftRadius: '6px' }}>
                        <div style={{ cursor: 'pointer', color: post.user_vote === 1 ? '#39d353' : '#8b949e', fontSize: '16px', transition: 'color 0.2s' }} onClick={() => handleVote(post.id, 1)}>▲</div>
                        <div style={{ color: post.user_vote === 1 ? '#39d353' : '#fff', fontWeight: 'bold', fontSize: '12px', margin: '2px 0' }}>{post.upvotes}</div>
                        <div style={{ height: '1px', width: '16px', background: '#30363d', margin: '4px 0' }} />
                        <div style={{ color: post.user_vote === -1 ? '#f85149' : '#fff', fontWeight: 'bold', fontSize: '12px', margin: '2px 0' }}>{post.downvotes}</div>
                        <div style={{ cursor: 'pointer', color: post.user_vote === -1 ? '#f85149' : '#8b949e', fontSize: '16px', transition: 'color 0.2s' }} onClick={() => handleVote(post.id, -1)}>▼</div>
                      </div>
                      {/* Content Block */}
                      <div style={{ flex: 1, padding: '16px' }}>
                        <div style={{ fontSize: '12px', color: '#8b949e', marginBottom: '8px', display: 'flex', alignItems: 'center', gap: '6px' }}>
                          <User size={12} /> <span style={{ color: '#58a6ff', fontWeight: '600' }}>u/{post.username}</span> • <span>{new Date(post.created_at).toLocaleDateString()}</span>
                        </div>
                        <NextLink href={`/community/post/${post.id}`} style={{ textDecoration: 'none' }}>
                          <h2 style={{ fontSize: '20px', color: '#fff', marginBottom: '12px', fontWeight: '600', letterSpacing: '-0.2px', cursor: 'pointer' }}>{post.title}</h2>
                        </NextLink>
                        
                        {/* Authoritative ReactMarkdown Rendering Cluster */}
                        <div className="pro-markdown-renderer" style={{ marginBottom: '16px' }}>
                          <ReactMarkdown 
                            components={{
                              img: ({node, ...props}) => <img {...props} style={{ maxWidth: '100%', maxHeight: '500px', border: '1px solid #30363d', borderRadius: '6px', margin: '12px 0', display: 'block' }} />
                            }}
                          >
                            {post.content || ''}
                          </ReactMarkdown>
                        </div>

                        <div style={{ display: 'flex', gap: '16px', fontSize: '12px', color: '#8b949e', borderTop: '1px solid #30363d', paddingTop: '10px' }}>
                          <button onClick={() => toggleComments(post.id)} style={{ background: 'none', border: 'none', color: '#8b949e', display: 'flex', alignItems: 'center', gap: '6px', cursor: 'pointer', padding: 0, fontSize: '12px', fontWeight: '500' }}>
                            <Activity size={14} /> {post.comments} Echoes
                          </button>
                          <button onClick={() => handleShare(post.id)} style={{ background: 'none', border: 'none', color: '#8b949e', display: 'flex', alignItems: 'center', gap: '4px', cursor: 'pointer', padding: 0, fontSize: '12px' }}>
                            <Share2 size={14} /> Share
                          </button>
                          <span onClick={handleReport} style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '4px' }}><Shield size={14} /> Report</span>
                        </div>

                        {/* Expanded Discussion Thread Tree */}
                        {openComments === post.id && (
                           <div style={{ marginTop: '16px', borderTop: '1px dashed #30363d', paddingTop: '16px' }}>
                              {/* Comment Input (Root) */}
                              <div style={{ display: 'flex', gap: '10px', marginBottom: '20px' }}>
                                <textarea 
                                   placeholder="Respond with clarification vector..."
                                   style={{ flex: 1, background: '#0d1117', border: '1px solid #30363d', color: '#c9d1d9', padding: '8px', fontSize: '13px', minHeight: '60px', borderRadius: '4px', resize: 'vertical' }}
                                   value={replyTo === null ? newCommentBody : ''}
                                   onChange={(e) => { setReplyTo(null); setNewCommentBody(e.target.value); }}
                                />
                                <button onClick={() => submitComment(post.id)} style={{ alignSelf: 'flex-end', background: '#21262d', border: '1px solid #30363d', color: '#c9d1d9', padding: '8px 12px', borderRadius: '4px', cursor: 'pointer', fontSize: '12px', fontWeight: 'bold' }}>Echo</button>
                              </div>

                              {/* Responses Feed with Full Recursion Rendering */}
                              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                                {renderTreeComments(post.id)}
                                {(commentsCache[post.id] || []).length === 0 && (
                                  <div style={{ fontSize: '12px', color: '#8b949e', fontStyle: 'italic', textAlign: 'center', padding: '20px' }}>Quiet channel. Be the first echo.</div>
                                )}
                              </div>
                           </div>
                        )}
                      </div>
                    </div>
                  ))
                ) : (
                  <div style={{ textAlign: 'center', padding: '40px', border: '1px dashed #30363d' }}>
                    <Activity size={40} style={{ color: '#30363d', marginBottom: '12px' }} />
                    <div style={{ color: '#8b949e' }}>Void detected. Launch the very first narrative node broadcast above.</div>
                  </div>
                )}
              </div>
            </div>
          ) : (
            /* ==============================================
               SETTINGS VIEW (Token Management Cluster)
               ============================================== */
            <div className="settings-view">
              <div className="page-header">
                <h1>Personal Access Tokens</h1>
                <div className="page-subtitle">Secure operational identifiers authorized to interface with the central registry node via CLI or external logic hooks.</div>
              </div>

              <div className="token-control-cluster">
                <h3 style={{ color: 'var(--terminal-green)', marginBottom: '8px' }}>Generate Fresh Credential</h3>
                <p style={{ color: '#8b949e', fontSize: '13px' }}>Specify a unique internal vector identifier to anchor new connection.</p>
                
                <div className="token-composer">
                  <input 
                    type="text" 
                    className="token-input" 
                    placeholder="Example: Workstation Alpha, Jenkins CI/CD"
                    value={newTokenName}
                    onChange={(e) => setNewTokenName(e.target.value)}
                  />
                  <button className="search-action-btn" onClick={handleCreateNewToken} style={{ padding: '0 24px' }}>
                    Provision Node
                  </button>
                </div>
              </div>

              <div className="section-header">
                <h2>Active Identifiers ({userTokens.length})</h2>
              </div>

              <div className="token-catalog">
                {userTokens.length === 0 ? (
                  <div style={{ padding: '32px', textAlign: 'center', color: '#8b949e', background: '#161b22' }}>
                    No standalone authorization nodes detected.
                  </div>
                ) : (
                  userTokens.map((t) => (
                    <div key={t.id} className="token-node">
                      <div className="token-identity">
                        <span className="token-name">{t.name}</span>
                        <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                          <span className="token-prefix-tag">{t.prefix}****************</span>
                          <span style={{ color: '#8b949e', fontSize: '11px' }}>Created: {new Date(t.created_at).toLocaleDateString()}</span>
                        </div>
                      </div>
                      <button className="revoke-trigger" onClick={() => handleRevokeToken(t.id)}>
                        Revoke Vector
                      </button>
                    </div>
                  ))
                )}
              </div>
            </div>
          )}
        </div>
      </main>

      {/* Dynamic Command Prompt Auth Modal */}
      {showAuth && (
        <div className="auth-modal-overlay">
          <div className="auth-modal">
            {needsVerification ? (
              <>
                <div className="modal-header">
                  <h3 className="modal-title" style={{ color: '#e3b341' }}>IDENTITY_CHALLENGE</h3>
                  <button className="close-btn" onClick={() => {setShowAuth(false); setNeedsVerification(false);}}>X</button>
                </div>
                
                <div style={{ margin: '12px 0', color: '#8b949e', fontSize: '13px' }}>
                  A confidential transmission has been routed to your vector. Input the numeric challenge sequence to anchor grid connectivity.
                </div>

                <div className="input-group">
                  <label className="input-label">Verification Code (6-Digits)</label>
                  <input 
                    type="text" 
                    className="auth-input" 
                    placeholder="000000" 
                    maxLength={6} 
                    style={{ fontSize: '24px', letterSpacing: '8px', textAlign: 'center', color: '#58a6ff' }}
                    value={verificationCode} 
                    onChange={(e) => setVerificationCode(e.target.value)} 
                    autoFocus 
                  />
                </div>

                <div className="action-group">
                  <button className="submit-btn" onClick={handleVerifySubmit} style={{ background: '#388bfd' }}>
                    Authorize Link
                  </button>
                  <button className="ghost-btn" onClick={() => setNeedsVerification(false)}>
                    Return to Index
                  </button>
                </div>
              </>
            ) : (
              <>
                <div className="modal-header">
                  <h3 className="modal-title">{isLoginMode ? '> AUTH_SESSION' : '> CREATE_USER'}</h3>
                  <button className="close-btn" onClick={() => setShowAuth(false)}>X</button>
                </div>
                
                <div className="input-group">
                  <label className="input-label">Username / Namespace</label>
                  <input type="text" className="auth-input" placeholder="Enter identity..." value={authUsername} onChange={(e) => setAuthUsername(e.target.value)} autoFocus />
                </div>

                {!isLoginMode && (
                  <div className="input-group">
                    <label className="input-label">Email Vector</label>
                    <input type="email" className="auth-input" placeholder="user@matrix.io" value={authEmail} onChange={(e) => setAuthEmail(e.target.value)} />
                  </div>
                )}

                <div className="input-group">
                  <label className="input-label">Access Key (Secret)</label>
                  <input type="password" className="auth-input" placeholder="••••••••" value={authPassword} onChange={(e) => setAuthPassword(e.target.value)} />
                </div>

                <div className="action-group">
                  <button className="submit-btn" onClick={handleAuthSubmit}>
                    {isLoginMode ? 'Initialize Link' : 'Provision Identity'}
                  </button>
                  <button className="ghost-btn" onClick={() => setIsLoginMode(!isLoginMode)}>
                    {isLoginMode ? 'Switch: Register New Node' : 'Switch: Existing Identity'}
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}
      {toast && (
        <div style={{
          position: 'fixed', bottom: '32px', right: '32px', zIndex: 9999,
          background: '#238636', color: '#fff', padding: '12px 24px',
          borderRadius: '6px', boxShadow: '0 8px 24px rgba(0,0,0,0.5)',
          fontFamily: 'ui-monospace, SFMono-Regular, monospace', fontSize: '13px',
          border: '1px solid rgba(255,255,255,0.2)', fontWeight: 'bold',
          animation: 'slideUpIn 0.3s cubic-bezier(0,0,0.2,1) forwards',
          whiteSpace: 'pre-line'
        }}>
          {toast}
        </div>
      )}
      
      <style jsx global>{`
        @keyframes slideUpIn {
          from { transform: translateY(20px); opacity: 0; }
          to { transform: translateY(0); opacity: 1; }
        }
        .pro-markdown-renderer h1, .pro-markdown-renderer h2, .pro-markdown-renderer h3 {
          border-bottom: 1px solid #30363d;
          padding-bottom: 6px;
          margin-top: 18px;
          margin-bottom: 12px;
          color: #fff;
        }
        .pro-markdown-renderer p {
          margin-bottom: 12px;
          color: #c9d1d9;
          font-size: 14px;
          line-height: 1.6;
        }
        .pro-markdown-renderer ul, .pro-markdown-renderer ol {
          padding-left: 24px;
          margin-bottom: 16px;
          color: #c9d1d9;
        }
        .pro-markdown-renderer pre {
          background: #0d1117;
          border: 1px solid #30363d;
          padding: 12px;
          border-radius: 6px;
          margin: 12px 0;
          overflow-x: auto;
        }
        .pro-markdown-renderer code {
          font-family: monospace;
          background: #21262d;
          padding: 2px 4px;
          border-radius: 4px;
          font-size: 13px;
        }
        .pro-markdown-renderer blockquote {
          border-left: 4px solid #30363d;
          padding-left: 16px;
          color: #8b949e;
          margin: 12px 0;
        }
      `}</style>
    </div>
  );
}

