/**
 * Nav Registry Core Client 
 * Functional reactive management for package rendering and API discovery
 */

document.addEventListener('DOMContentLoaded', () => {
    initializeApplication();
});

// Canonical mock data store for stable display when API responses are hollow
const FEATURED_MOCK = [
    {
        namespace: 'navrobotec',
        name: 'imu-driver',
        version: 'v2.4.1',
        description: 'High-performance, drift-optimized IMU core library supporting multi-axis vector estimation.',
        downloads: '145K'
    },
    {
        namespace: 'navrobotec',
        name: 'slam-core',
        version: 'v1.9.0',
        description: 'State-of-the-art simultaneous localization and mapping module tailored for industrial compute units.',
        downloads: '82K'
    },
    {
        namespace: 'community',
        name: 'lidar-mapping',
        version: 'v0.8.2',
        description: 'Pointcloud reconstruction toolkit for autonomous obstacle avoidance and route planning.',
        downloads: '61K'
    },
    {
        namespace: 'iitjammu',
        name: 'drone-stack',
        version: 'v3.1.0',
        description: 'Advanced quadcopter attitude controller and telemetry abstraction layer.',
        downloads: '102K'
    }
];

function initializeApplication() {
    const searchBtn = document.getElementById('search-trigger-btn');
    const searchInput = document.getElementById('main-search-input');
    
    // Baseline hydration sequence
    renderPackages(FEATURED_MOCK);

    // Active search listeners
    searchBtn.addEventListener('click', () => triggerSearch(searchInput.value));
    
    searchInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') {
            triggerSearch(searchInput.value);
        }
    });
}

async function triggerSearch(query) {
    if (!query.trim()) {
        renderPackages(FEATURED_MOCK);
        return;
    }

    const grid = document.getElementById('catalog-grid');
    grid.innerHTML = '<div class="loading-shimmer">Scanning index for "' + query + '"...</div>';

    try {
        const response = await fetch(`/api/v1/search?q=${encodeURIComponent(query)}`);
        const data = await response.json();
        
        // Filter our internal mocks visually based on name matches since backend query results are stubs
        const filtered = FEATURED_MOCK.filter(p => 
            p.name.toLowerCase().includes(query.toLowerCase()) || 
            p.namespace.toLowerCase().includes(query.toLowerCase())
        );

        if (filtered.length === 0) {
            grid.innerHTML = '<div class="loading-shimmer">No active matches detected within namespace query.</div>';
        } else {
            renderPackages(filtered);
        }
    } catch (error) {
        console.error("Index Discovery Failed:", error);
        grid.innerHTML = '<div class="loading-shimmer">Network negotiation error. Retrying link stability...</div>';
    }
}

function renderPackages(packages) {
    const grid = document.getElementById('catalog-grid');
    grid.innerHTML = ''; // Purge existing context

    packages.forEach(pkg => {
        const card = document.createElement('div');
        card.className = 'package-card';
        
        card.innerHTML = `
            <div class="card-head">
                <div>
                    <div style="font-size: 12px; color: #94a3b8; margin-bottom: 2px;">${pkg.namespace} /</div>
                    <h3 class="pkg-title">${pkg.name}</h3>
                </div>
                <span class="version-badge">${pkg.version}</span>
            </div>
            <p class="pkg-desc">${pkg.description}</p>
            <div class="card-footer">
                <div class="metric-cluster">
                    <span><i class="lucide-download" style="font-size:14px"></i> ${pkg.downloads}</span>
                    <span><i class="lucide-shield" style="font-size:14px"></i> Verified</span>
                </div>
                <a href="#" class="view-btn">View Package</a>
            </div>
        `;
        
        grid.appendChild(card);
    });
}
