/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

(function() {
    let availableWorlds = [];

    window.onWorldsUpdate = function(worlds) {
        if (typeof __mvrLog !== 'undefined') __mvrLog.info('onWorldsUpdate: received ' + worlds.length + ' worlds');
        availableWorlds = worlds;
        // Update the global WORLDS array for the topbar menu
        if (typeof WORLDS !== 'undefined') {
            window.WORLDS = worlds.map(w => ({
                id: w.name,
                hint: w.path,
                path: w.path,
                is_running: w.is_running,
                is_active: w.is_active
            }));
        }
        renderWorlds();
        if (typeof renderTopbar === 'function') renderTopbar();
        if (typeof syncLeftTabs === 'function') syncLeftTabs();
    };

    window.renderWorlds = renderWorlds;

    function renderWorlds() {
        const container = document.getElementById('worlds-list');
        if (!container) return;

        if (availableWorlds.length === 0) {
            container.innerHTML = '<div class="empty-state">No worlds found in worlds/ directory</div>';
            return;
        }

        container.innerHTML = '';
        availableWorlds.forEach(world => {
            const el = document.createElement('div');
            el.className = 'world-item' + (world.is_active ? ' active' : '') + (world.is_running ? ' running' : '');
            
            const info = document.createElement('div');
            info.className = 'world-info';
            
            const name = document.createElement('div');
            name.className = 'world-name';
            name.textContent = world.name;
            info.appendChild(name);
            
            const path = document.createElement('div');
            path.className = 'world-path';
            path.textContent = world.path;
            info.appendChild(path);
            
            el.appendChild(info);
            
            const actions = document.createElement('div');
            actions.className = 'world-actions';
            
            if (!world.is_active) {
                const loadBtn = document.createElement('button');
                loadBtn.textContent = world.is_running ? 'Switch' : 'Load';
                loadBtn.onclick = () => {
                    document.title = "mvr:switch_world:" + world.path;
                };
                actions.appendChild(loadBtn);
            } else {
                const activeBadge = document.createElement('span');
                activeBadge.className = 'badge active';
                activeBadge.textContent = 'Active';
                actions.appendChild(activeBadge);
            }
            
            if (!world.is_running) {
                const bgBtn = document.createElement('button');
                bgBtn.className = 'secondary';
                bgBtn.textContent = 'Load Background';
                bgBtn.onclick = () => {
                    document.title = "mvr:load_world_background:" + world.path;
                };
                actions.appendChild(bgBtn);
            } else if (!world.is_active) {
                const runningBadge = document.createElement('span');
                runningBadge.className = 'badge running';
                runningBadge.textContent = 'Running (BG)';
                actions.appendChild(runningBadge);
            }
            
            el.appendChild(actions);
            container.appendChild(el);
        });
    }

    // Register a tab for worlds if the UI supports it
    if (window.registerTab) {
        window.registerTab('worlds', 'Worlds', `
            <div id="worlds-panel" class="panel">
                <div class="panel-header">
                    <h3>Available Worlds</h3>
                    <div class="header-actions">
                        <button onclick="document.title='mvr:refresh_worlds'">Refresh</button>
                    </div>
                </div>
                <div id="worlds-list" class="panel-content">
                    <div class="loading">Scanning worlds directory...</div>
                </div>
            </div>
        `, () => {
            renderWorlds();
        });
    }

    // Initial request for worlds
    document.title = 'mvr:refresh_worlds';
})();
