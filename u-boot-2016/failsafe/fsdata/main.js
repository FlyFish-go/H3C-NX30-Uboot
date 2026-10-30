// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 chenxin527. All Rights Reserved.
 *
 * This file is part of the project uboot-qsdk12.5-build
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * 注：引用的代码已注明出处。
 */

/**
 * 全局应用状态
 * 注：该对象来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
const APP_STATE = {
    lang: "en",
    theme: "auto",
    page: ""
};

// ==============================
// 工具函数
// ==============================

/**
 * 标准化语言代码
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function normalizeLang(lang) {
    if (!lang) return "en";
    const lowerLang = String(lang).toLowerCase();
    return lowerLang.indexOf("zh") === 0 ? "zh-cn" : "en";
}

/**
 * 检测用户语言偏好
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function detectLang() {
    // 尝试从 localStorage 读取
    try {
        const savedLang = localStorage.getItem("lang");
        if (savedLang) return normalizeLang(savedLang);
    } catch (error) {
        console.warn("Failed to read language from localStorage:", error);
    }

    // 从浏览器检测
    const browserLanguages = navigator.languages ||
                            (navigator.language ? [navigator.language] : []);
    return normalizeLang(browserLanguages[0]);
}

/**
 * 检测主题偏好
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function detectTheme() {
    try {
        const savedTheme = localStorage.getItem("theme");
        if (savedTheme) return savedTheme;
    } catch (error) {
        console.warn("Failed to read theme from localStorage:", error);
    }
    return "auto";
}

/**
 * 翻译函数
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function isRt3000BProtectedEnvName(name, bPersistentMode) {
    return bPersistentMode && ["bootcmd", "bootargs", "bootdelay", "ethact", "mtdparts", "mtdids"].includes(name);
}

function t(key) {
    const lang = APP_STATE.lang || "en";

    // 尝试目标语言
    if (I18N[lang] && I18N[lang][key] !== undefined) {
        return I18N[lang][key];
    }

    // 回退到英语
    if (I18N.en && I18N.en[key] !== undefined) {
        return I18N.en[key];
    }

    // 返回键名作为最后的回退
    return key;
}

/**
 * 应用国际化到指定元素或整个文档
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function applyI18n(element) {
    const container = element || document;

    // 更新文本内容
    const textElements = container.querySelectorAll("[data-i18n]");
    textElements.forEach(el => {
        const key = el.getAttribute("data-i18n");
        el.textContent = t(key);
    });

    // 更新HTML内容
    const htmlElements = container.querySelectorAll("[data-i18n-html]");
    htmlElements.forEach(el => {
        const key = el.getAttribute("data-i18n-html");
        el.innerHTML = t(key);
    });

    // 更新属性
    const attrElements = container.querySelectorAll("[data-i18n-attr]");
    attrElements.forEach(el => {
        const attrSpec = el.getAttribute("data-i18n-attr");
        const [attrName, ...keyParts] = attrSpec.split(":");
        const key = keyParts.join(":");

        if (attrName && key) {
            el.setAttribute(attrName, t(key));
        }
    });
}

/**
 * 设置语言
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function setLang(lang) {
    APP_STATE.lang = normalizeLang(lang);

    try {
        localStorage.setItem("lang", APP_STATE.lang);
    } catch (error) {
        console.warn("Failed to save language to localStorage:", error);
    }

    applyI18n(document);

    // 更新特定组件
    if (typeof updateSidebarI18n === "function") {
        updateSidebarI18n();
    }

    // 更新设置面板国际化
    if (typeof updateSettingsI18n === "function") {
        updateSettingsI18n();
    }

    updateDocumentTitle();
}

/**
 * 设置主题
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function setTheme(theme) {
    APP_STATE.theme = theme || "auto";

    try {
        localStorage.setItem("theme", APP_STATE.theme);
    } catch (error) {
        console.warn("Failed to save theme to localStorage:", error);
    }

    const htmlElement = document.documentElement;

    if (APP_STATE.theme === "auto") {
        htmlElement.removeAttribute("data-theme");
    } else {
        htmlElement.setAttribute("data-theme", APP_STATE.theme);
    }

    // 更新设置面板中的选择器值
    const themeSelect = document.getElementById('theme_select_settings');
    if (themeSelect) {
        themeSelect.value = APP_STATE.theme;
    }
}

/**
 * 更新文档标题
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function updateDocumentTitle() {
    if (!APP_STATE.page) return;

    const key = APP_STATE.page + ".title";

    if (I18N[APP_STATE.lang] && I18N[APP_STATE.lang][key]) {
        document.title = t(key);
        return;
    }

    // 处理特殊页面
    if (APP_STATE.page === "reboot") {
        document.title = t("reboot.title.in_progress");
    }
}

/**
 * 将字节转换为可读格式
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function bytesToHuman(bytes) {
    if (bytes === null || bytes === undefined) return "";

    const numBytes = Number(bytes);
    if (!isFinite(numBytes) || numBytes < 0) return "";

    if (numBytes >= 1024 * 1024 * 1024) {
        return (numBytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB";
    } else if (numBytes >= 1024 * 1024) {
        return (numBytes / (1024 * 1024)).toFixed(2) + " MiB";
    } else if (numBytes >= 1024) {
        return (numBytes / 1024).toFixed(2) + " KiB";
    } else {
        return String(Math.floor(numBytes)) + " B";
    }
}

/**
 * 封装 AJAX 请求
 * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function ajax(options) {
    const xhr = window.XMLHttpRequest ?
                new XMLHttpRequest() :
                new ActiveXObject("Microsoft.XMLHTTP");

    // 上传进度事件
    if (options.progress && xhr.upload) {
        xhr.upload.addEventListener("progress", options.progress);
    }

    // 状态变化事件
    xhr.onreadystatechange = function() {
        if (xhr.readyState === 4 && xhr.status === 200 && options.done) {
            options.done(xhr.responseText);
        }
    }

    // 超时设置
    if (options.timeout) {
        xhr.timeout = options.timeout;
    }

    // 发送请求
    const method = options.data ? "POST" : "GET";
    xhr.open(method, options.url);
    xhr.send(options.data);
}

/**
 * HTML转义（防止XSS）
 */
function escapeHtml(str) {
    if (!str) return "";
    return String(str)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

/**
 * 检查是否处于 9008 模式
 * @returns {boolean}
 */
function is9008Mode() {
    const sysinfo = APP_STATE.sysinfo;
    return sysinfo && sysinfo.is_9008_mode;
}

// ==============================
// 侧边导航栏
// ==============================

/**
 * 侧边栏管理器
 * 负责动态生成和更新侧边栏导航
 */
class SidebarManager {
    constructor() {
        this.sidebarElement = null;
        this.currentPath = this.getCurrentPath();
        this.currentPageId = this.getCurrentPageId();
        this.navigationSections = this.getNavigationConfig();
        this.rebootItem = this.getRebootConfig();
    }

    /**
     * 获取当前页面路径
     */
    getCurrentPath() {
        const pathname = location && location.pathname ? location.pathname : "";
        return pathname === "" ? "/" : pathname;
    }

    /**
     * 获取当前页面ID
     */
    getCurrentPageId() {
        const path = this.getCurrentPath();

        // 映射路径到页面ID
        const pathMap = {
            '/': 'sysinfo',
            '/cgi-bin/luci': 'sysinfo',
            '/cgi-bin/luci/': 'sysinfo',
            '/index.html': 'sysinfo',
            '/syslog.html': 'syslog',
            '/firmware.html': 'firmware',
            '/firmware-dual.html': 'firmware',
            '/firmware-single.html': 'firmware',
            '/art.html': 'art',
            '/cdt.html': 'cdt',
            '/ptable.html': 'ptable',
            '/simg.html': 'simg',
            '/uboot.html': 'uboot',
            '/initramfs.html': 'initramfs',
            '/mibib.html': 'mibib',
            '/backup.html': 'backup',
            '/env.html': 'env',
            '/mac.html': 'mac',
            '/network.html': 'network',
            '/settings.html': 'settings',
            '/webterm.html': 'webterm',
            '/flashing.html': 'flashing',
            '/booting.html': 'booting',
            '/reboot.html': 'reboot',
        };

        return pathMap[path] || null;
    }

    /**
     * 重启功能配置（独立一级菜单）
     */
    getRebootConfig() {
        return {
            titleKey: "nav.reboot",
            id: "reboot",
            path: "/reboot.html",
            // 点击时弹出确认框，确认后跳转到 reboot.html
            onClick: () => {
                return confirm(t("reboot.confirm"));
            }
        };
    }

    /**
     * 导航配置
     */
    getNavigationConfig() {
        if (document.body.classList.contains("nx30-d-ram-readonly")) {
            return { overview: {
                titleKey: "nav.overview",
                items: [{ path: "/", labelKey: "nav.sysinfo", id: "sysinfo" }]
            } };
        }
        return {
            overview: {
                titleKey: "nav.overview",
                items: [
                    { path: "/", labelKey: "nav.sysinfo", id: "sysinfo" },
                    { path: "/syslog.html", labelKey: "nav.syslog", id: "syslog" }
                ]
            },
            update: {
                titleKey: "nav.update",
                items: [
                    { path: "/firmware.html", labelKey: "nav.firmware", id: "firmware" },
                    { path: "/art.html", labelKey: "nav.art", id: "art" },
                    { path: "/cdt.html", labelKey: "nav.cdt", id: "cdt" },
                    { path: "/ptable.html", labelKey: "nav.ptable", id: "ptable" },
                    { path: "/simg.html", labelKey: "nav.simg", id: "simg" },
                    { path: "/uboot.html", labelKey: "nav.uboot", id: "uboot" }
                ]
            },
            debrick: {
                titleKey: "nav.debrick",
                items: [
                    { path: "/mibib.html", labelKey: "nav.mibib", id: "mibib" },
                    { path: "/initramfs.html", labelKey: "nav.initramfs", id: "initramfs" }
                ]
            },
            system: {
                titleKey: "nav.system",
                items: [
                    { path: "/settings.html", labelKey: "nav.settings", id: "settings" },
                    { path: "/network.html", labelKey: "nav.network", id: "network" },
                    { path: "/mac.html", labelKey: "nav.mac", id: "mac" },
                    { path: "/env.html", labelKey: "nav.env", id: "env" },
                    { path: "/backup.html", labelKey: "nav.backup", id: "backup" },
                    { path: "/webterm.html", labelKey: "nav.webterm", id: "webterm" }
                ]
            }
        };
    }

    /**
     * 初始化侧边栏
     */
    init() {
        this.sidebarElement = document.getElementById("sidebar");
        if (!this.sidebarElement) {
            console.warn("Sidebar element not found");
            return;
        }

        // 检查是否已渲染
        if (this.sidebarElement.getAttribute("data-rendered") === "1") {
            // 如果已渲染，只更新状态而不重新渲染
            this.updateActiveStates();
            return;
        }

        this.render();
    }

    /**
     * 渲染整个侧边栏
     */
    render() {
        // 清空并标记为已渲染
        this.sidebarElement.innerHTML = "";
        this.sidebarElement.setAttribute("data-rendered", "1");

        // 构建侧边栏各组件
        this.renderBrandSection();
        this.renderNavigationSections();

        // 应用国际化
        applyI18n(this.sidebarElement);

        // 更新激活状态
        this.updateActiveStates();
    }

    /**
     * 创建品牌标题区
     */
    renderBrandSection() {
        const brandContainer = document.createElement("div");
        brandContainer.className = "sidebar-brand";

        const title = document.createElement("div");
        title.className = "title";
        title.setAttribute("data-i18n", "app.name");
        title.textContent = t("app.name");

        brandContainer.appendChild(title);
        this.sidebarElement.appendChild(brandContainer);
    }

    /**
     * 渲染所有导航部分
     */
    renderNavigationSections() {
        const navContainer = document.createElement("div");
        navContainer.className = "nav";

        // 按顺序渲染各导航部分
        Object.entries(this.navigationSections).forEach(([sectionKey, sectionConfig]) => {
            navContainer.appendChild(this.createNavigationSection(sectionKey, sectionConfig));
        });

        if (!document.body.classList.contains("nx30-d-ram-readonly"))
            navContainer.appendChild(this.createRebootSection());

        this.sidebarElement.appendChild(navContainer);

        // 初始化折叠状态 - 只展开当前页面所在section
        this.initCollapsibleSections();
    }

    /**
     * 渲染重启区域（独立一级菜单，无子项）
     */
    createRebootSection() {
        // 创建重启按钮（作为一级菜单）
        const sectionElement = document.createElement("div");
        sectionElement.className = "nav-section reboot-section";
        sectionElement.dataset.section = "reboot";

        const rebootButton = document.createElement("div");
        rebootButton.className = "nav-section-title";
        rebootButton.setAttribute("data-i18n", this.rebootItem.titleKey);
        rebootButton.textContent = t(this.rebootItem.titleKey);

        // 添加点击事件 - 弹出确认框，确认后跳转到 reboot.html
        rebootButton.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();

            // 弹出确认框
            if (this.rebootItem.onClick && this.rebootItem.onClick()) {
                // 用户确认，跳转到 reboot.html
                window.location.href = this.rebootItem.path;
            }
            // 用户取消，什么都不做
        });

        sectionElement.appendChild(rebootButton);

        return sectionElement;
    }

    /**
     * 创建单个导航部分
     */
    createNavigationSection(sectionKey, sectionConfig) {
        const sectionElement = document.createElement("div");
        sectionElement.className = "nav-section";
        sectionElement.dataset.section = sectionKey;

        // 添加标题 - 作为折叠按钮
        const titleElement = document.createElement("div");
        titleElement.className = "nav-section-title";
        titleElement.setAttribute("data-i18n", sectionConfig.titleKey);
        titleElement.textContent = t(sectionConfig.titleKey);
        sectionElement.appendChild(titleElement);

        // 创建导航项容器
        const itemsContainer = document.createElement("div");
        itemsContainer.className = "nav-items";

        // 添加导航项
        sectionConfig.items.forEach(item => {
            itemsContainer.appendChild(this.createNavLink(item));
        });

        sectionElement.appendChild(itemsContainer);

        return sectionElement;
    }

    /**
     * 创建单个导航链接
     */
    createNavLink(item) {
        const link = document.createElement("a");
        link.className = "nav-link";
        link.href = item.path;
        link.setAttribute("data-nav-id", item.id);

        // 文本标签
        const label = document.createElement("span");
        label.setAttribute("data-i18n", item.labelKey);
        label.textContent = t(item.labelKey);
        link.appendChild(label);

        return link;
    }

    /**
     * 初始化可折叠菜单
     * 规则：只展开当前页面所在的section，其他全部折叠
     */
    initCollapsibleSections() {
        const sections = this.sidebarElement.querySelectorAll('.nav-section:not(.reboot-section)');

        // 先找出当前页面所在的section
        const activeSectionKey = this.findActiveSection();

        sections.forEach((section) => {
            const title = section.querySelector('.nav-section-title');
            const sectionKey = section.dataset.section;

            // 移除旧的点击事件监听器（避免重复绑定）
            const newTitle = title.cloneNode(true);
            title.parentNode.replaceChild(newTitle, title);

            // 绑定新的点击事件
            newTitle.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.toggleSection(section, sectionKey);
            });

            // 根据当前页面所在section设置折叠状态
            if (activeSectionKey === sectionKey) {
                // 当前页面所在section展开
                section.classList.remove('collapsed');
                // 添加激活section类
                section.classList.add('active-section');
            } else {
                // 其他section全部折叠
                section.classList.add('collapsed');
                section.classList.remove('active-section');
            }
        });
    }

    /**
     * 查找当前页面所在的section
     */
    findActiveSection() {
        if (!this.currentPageId) return null;

        if (this.currentPageId === 'reboot') return 'reboot';

        for (const [sectionKey, sectionConfig] of Object.entries(this.navigationSections)) {
            const found = sectionConfig.items.some(item => item.id === this.currentPageId);
            if (found) {
                return sectionKey;
            }
        }
        return null;
    }

    /**
     * 更新所有导航项的激活状态
     */
    updateActiveStates() {
        if (!this.sidebarElement) return;

        // 移除所有active类
        const allLinks = this.sidebarElement.querySelectorAll('.nav-link');
        allLinks.forEach(link => link.classList.remove('active'));

        // 为当前页面的链接添加active类
        allLinks.forEach(link => {
            const navId = link.getAttribute('data-nav-id');
            if (navId === this.currentPageId) {
                link.classList.add('active');
            }
        });

        // 更新当前页面所在section的激活状态
        this.updateActiveSection();
    }

    /**
     * 更新当前页面所在section的激活状态
     */
    updateActiveSection() {
        if (!this.sidebarElement) return;

        // 移除所有section的active-section类
        const allSections = this.sidebarElement.querySelectorAll('.nav-section');
        allSections.forEach(section => {
            section.classList.remove('active-section');
        });

        // 为当前页面所在的section添加active-section类
        const activeSectionKey = this.findActiveSection();
        if (activeSectionKey) {
            const activeSection = this.sidebarElement.querySelector(`.nav-section[data-section="${activeSectionKey}"]`);
            if (activeSection) {
                activeSection.classList.add('active-section');
            }
        }
    }

    /**
     * 切换部分折叠状态
     * 点击标题时切换当前section的折叠状态
     */
    toggleSection(section, sectionKey) {
        // 切换当前部分的折叠状态
        section.classList.toggle('collapsed');
    }

    /**
     * 更新导航项状态（语言切换后）
     */
    updateNavigationLabels() {
        if (!this.sidebarElement) return;

        // 更新所有导航标签的文本
        const navLabels = this.sidebarElement.querySelectorAll("[data-i18n]");
        navLabels.forEach(label => {
            const key = label.getAttribute("data-i18n");
            if (key) {
                label.textContent = t(key);
            }
        });

        // 刷新折叠状态 - 保持只展开当前页面所在section
        this.refreshCollapsibleState();
    }

    /**
     * 刷新折叠状态（不重建DOM）
     * 规则：只展开当前页面所在section，其他全部折叠
     */
    refreshCollapsibleState() {
        const sections = this.sidebarElement.querySelectorAll('.nav-section:not(.reboot-section)');
        const activeSectionKey = this.findActiveSection();

        sections.forEach((section) => {
            const sectionKey = section.dataset.section;

            // 根据当前页面所在section设置折叠状态
            if (activeSectionKey === sectionKey) {
                // 当前页面所在section展开
                section.classList.remove('collapsed');
                section.classList.add('active-section');
            } else {
                // 其他section全部折叠
                section.classList.add('collapsed');
                section.classList.remove('active-section');
            }
        });
    }
}

// ==============================
// 侧边栏收起/唤出功能
// ==============================

/**
 * 侧边栏管理器增强版
 * 添加收起/唤出功能
 */
class EnhancedSidebarManager extends SidebarManager {
    constructor() {
        super();
        this.isMobile = this.checkIsMobile();
        this.isCollapsed = false;
        this.toggleButton = null;
        this.overlay = null;
    }

    /**
     * 检查是否为移动设备
     */
    checkIsMobile() {
        return window.innerWidth <= 992; // 平板及以下视为移动设备
    }

    /**
     * 初始化增强功能
     */
    initEnhanced() {
        this.toggleButton = document.getElementById('sidebarToggle');
        this.overlay = document.getElementById('sidebarOverlay');

        if (!this.toggleButton || !this.sidebarElement) {
            return;
        }

        // 加载保存的状态
        this.loadSidebarState();

        // 绑定事件
        this.bindEvents();
        this.bindKeyboardEvents();

        // 初始状态设置
        this.updateSidebarState();

        // 监听窗口大小变化
        window.addEventListener('resize', () => {
            this.handleResize();
        });
    }

    /**
     * 加载侧边栏状态
     */
    loadSidebarState() {
        try {
            if (this.isMobile) {
                // 移动端默认收起
                this.isCollapsed = true;
            } else {
                // 桌面端尝试加载保存的状态
                const savedState = localStorage.getItem('sidebarCollapsed');
                if (savedState !== null) {
                    this.isCollapsed = savedState === 'true';
                }
            }
        } catch (error) {
            console.warn('Failed to load sidebar state:', error);
            this.isCollapsed = this.isMobile;
        }
    }

    /**
     * 保存侧边栏状态
     */
    saveSidebarState() {
        try {
            if (!this.isMobile) {
                localStorage.setItem('sidebarCollapsed', this.isCollapsed.toString());
            }
        } catch (error) {
            console.warn('Failed to save sidebar state:', error);
        }
    }

    /**
     * 绑定事件
     */
    bindEvents() {
        // 切换按钮点击事件
        this.toggleButton.addEventListener('click', (e) => {
            e.stopPropagation();
            this.toggleSidebar();
        });

        // 遮罩层点击事件
        if (this.overlay) {
            this.overlay.addEventListener('click', () => {
                this.collapseSidebar();
            });
        }

        // 点击侧边栏外部时收起（仅移动端）
        document.addEventListener('click', (e) => {
            if (this.isMobile && this.isExpanded() &&
                !this.sidebarElement.contains(e.target) &&
                e.target !== this.toggleButton) {
                this.collapseSidebar();
            }
        });

        // 阻止侧边栏内点击事件冒泡
        this.sidebarElement.addEventListener('click', (e) => {
            e.stopPropagation();
        });
    }

    /**
     * 绑定键盘事件
     */
    bindKeyboardEvents() {
        document.addEventListener('keydown', (e) => {
            // Ctrl + B 或 Cmd + B 切换侧边栏
            if ((e.ctrlKey || e.metaKey) && e.key === 'b') {
                this.toggleSidebar();
                e.preventDefault();
            }
        });
    }

    /**
     * 切换侧边栏状态
     */
    toggleSidebar() {
        this.isCollapsed = !this.isCollapsed;
        this.updateSidebarState();
        this.saveSidebarState();
    }

    /**
     * 展开侧边栏
     */
    expandSidebar() {
        this.isCollapsed = false;
        this.updateSidebarState();
        this.saveSidebarState();
    }

    /**
     * 收起侧边栏
     */
    collapseSidebar() {
        this.isCollapsed = true;
        this.updateSidebarState();
        this.saveSidebarState();
    }

    /**
     * 检查侧边栏是否展开
     */
    isExpanded() {
        return !this.isCollapsed;
    }

    /**
     * 更新侧边栏状态
     */
    updateSidebarState() {
        const isExpanded = !this.isCollapsed;

        // 更新侧边栏类名
        this.sidebarElement.classList.toggle('collapsed', this.isCollapsed);
        this.sidebarElement.classList.toggle('expanded', isExpanded);

        // 更新切换按钮显示状态
        this.toggleButton.style.display = isExpanded ? "none" : "flex";

        // 更新遮罩层
        if (this.overlay) {
            this.overlay.classList.toggle('active', isExpanded && this.isMobile);
        }

        // 更新 body 类名（移动端防止滚动）
        document.body.classList.toggle('sidebar-expanded', isExpanded && this.isMobile);
    }

    /**
     * 处理窗口大小变化
     */
    handleResize() {
        const wasMobile = this.isMobile;
        this.isMobile = this.checkIsMobile();

        // 如果移动状态发生变化
        if (wasMobile !== this.isMobile) {
            if (this.isMobile) {
                // 切换到移动端：默认收起
                this.isCollapsed = true;
            } else {
                // 切换到桌面端：恢复保存的状态或默认展开
                try {
                    const savedState = localStorage.getItem('sidebarCollapsed');
                    this.isCollapsed = savedState ? savedState === 'true' : false;
                } catch (error) {
                    this.isCollapsed = false;
                }
            }
            this.updateSidebarState();
        }
    }
}

/**
 * 全局增强侧边栏实例
 */
let enhancedSidebarManager = null;

/**
 * 初始化侧边栏增强功能
 */
function initEnhancedSidebar() {
    if (!enhancedSidebarManager) {
        enhancedSidebarManager = new EnhancedSidebarManager();
    }

    // 先初始化基础侧边栏
    enhancedSidebarManager.init();

    // 再初始化增强功能
    enhancedSidebarManager.initEnhanced();
}

/**
 * 初始化侧边栏
 */
function ensureSidebar() {
    initEnhancedSidebar();
}

/**
 * 更新侧边栏国际化文本
 */
function updateSidebarI18n() {
    if (enhancedSidebarManager) {
        enhancedSidebarManager.updateNavigationLabels();
    }
}

// ==============================
// 版本信息模块
// ==============================

/**
 * 版本信息渲染器
 * 负责获取 U-Boot 版本信息并渲染到页脚
 */
const versionRenderer = (() => {
    /**
     * 渲染版本信息
     */
    function render(versionText) {
        const versionEl = document.getElementById("version");
        if (!versionEl) return;

        versionEl.innerHTML = `
            <a href="https://github.com/chenxin527/uboot-qsdk12.5-build"
               target="_blank"
               class="version-link"
               title="${t("title.project")}"
               data-i18n-attr="title:title.project">
                ${versionText || "U-Boot QSDK 12.5"}
            </a>
            <span class="separator">/</span>
            <a href="https://github.com/chenxin527"
               target="_blank"
               class="author-link"
               title="${t("title.author")}"
               data-i18n-attr="title:title.author">
                沉心
            </a>
        `;
    }

    /**
     * 加载并渲染版本信息
     */
    async function loadAndRender() {
        const versionEl = document.getElementById("version");
        if (!versionEl) return;

        try {
            const response = await fetch("/version", { method: "GET" });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const versionText = await response.text();
            render(versionText);

        } catch (error) {
            console.warn(error.message + ": Failed to fetch version");
            render();
        }
    }

    return {
        loadAndRender
    };
})();

// ==============================
// 应用初始化模块
// ==============================

/**
 * 页面配置映射
 */
const pageConfigs = {
    firmware: {
        needUpload: false,
        init: () => firmwareModeManager.initLanding()
    },
    'firmware-dual': {
        needUpload: false,
        init: () => firmwareModeManager.initMode('dual')
    },
    'firmware-single': {
        needUpload: false,
        init: () => firmwareModeManager.initMode('single')
    },
    uboot: {
        needUpload: true,
        formDataKey: 'uboot',
        warningItems: {
            common: true,
            custom: 2
        }
    },
    art: {
        needUpload: true,
        formDataKey: 'art',
        warningItems:  {
            common: true,
            custom: 2
        }
    },
    cdt: {
        needUpload: true,
        formDataKey: 'cdt',
        warningItems:  {
            common: true,
            custom: 2
        }
    },
    ptable: {
        needUpload: true,
        formDataKey: 'ptable',
        warningItems:  {
            common: true,
            custom: 2
        }
    },
    simg: {
        needUpload: true,
        formDataKey: 'simg',
        warningItems:  {
            common: true,
            custom: 2
        }
    },
    initramfs: {
        needUpload: true,
        formDataKey: 'initramfs',
        warningItems:  {
            common: false,
            custom: 2
        }
    },
    mibib: {
        needUpload: true,
        formDataKey: 'mibib',
        warningItems:  {
            common: false,
            custom: 2
        }
    },
    env: {
        needUpload: false,
        init: () => envManager.init()
    },
    backup: {
        needUpload: false,
        init: () => backupManager.init()
    },
    mac: {
        needUpload: false,
        init: () => macManager.init()
    },
    network: {
        needUpload: false,
        init: () => networkManager.init()
    },
    webterm: {
        needUpload: false,
        init: () => webTerminalManager.init()
    },
    settings: {
        needUpload: false,
        init: () => settingsPageManager.init()
    },
    sysinfo: {
        needUpload: false,
        init: () => sysinfoManager.init()
    },
    syslog: {
        needUpload: false,
        init: () => syslogManager.init()
    },
};

/**
 * 应用初始化
 * 注：该函数修改自项目：Yuzhii0718/bl-mt798x-dhcpd
 */
function appInit(pageName) {
    // 初始化应用状态
    APP_STATE.page = pageName || "";
    APP_STATE.lang = detectLang();
    APP_STATE.theme = detectTheme();

    // 获取页面配置
    const config = pageConfigs[pageName] || { needUpload: false };

    // 初始化上传组件（如果需要）
    if (config.needUpload) {
        const uploadManager = getUnifiedUploadManager();
        uploadManager.initForPage(pageName, {
            formDataKey: config.formDataKey || pageName,
            warningItems : config.warningItems || null
        });
    }

    // 应用主题和语言
    setTheme(APP_STATE.theme);
    setLang(APP_STATE.lang);

    // 初始化侧边栏（增强版）
    ensureSidebar();

    // 应用国际化
    applyI18n(document);

    // 更新文档标题
    updateDocumentTitle();

    // 执行页面特定的初始化
    if (config.init && typeof config.init === 'function') {
        config.init();
    } else if (!APP_STATE.sysinfo) {
        sysinfoManager.fetchAndStore();
    }

    if (pageName === 'sysinfo') {
        bootSlotsManager.init();
        if (!document.body.classList.contains("nx30-d-ram-readonly"))
            boardButtonsManager.init();
    }

    // 添加准备完成的类
    setTimeout(function() {
        document.body.classList.add("ready");
    }, 0);

    // 获取并渲染版本信息
    versionRenderer.loadAndRender();
}

// ==============================
// 信息生成工具模块
// ==============================

/**
 * 信息构建器
 * 提供成功信息与各种错误类型信息的结构化 HTML 生成函数
 */
const messageBuilder = (() => {
    /**
     * 生成标准错误表格
     * @param {string} title - 错误标题（国际化 key）
     * @param {Array<{label: string, value: any}>} rows - 标签和值的数组
     * @param {string} [suggestion] - 错误建议（国际化 key）
     * @returns {string} HTML字符串
     */
    function buildErrorTable(title, rows, suggestion) {
        const titleText = t(title) || title;
        const filteredRows = rows.filter(item => item.value != null && item.value !== undefined);

        let html = `
            <div class="error-title">❌ ${titleText}</div>
            <table class="info-table error-table">
                <tbody>
                    ${filteredRows.map(item => `
                    <tr>
                        <td class="info-label">${t(item.label)}</td>
                        <td class="info-value">${escapeHtml(String(item.value))}</td>
                    </tr>`).join('')}
                </tbody>
            </table>`;

        if (suggestion) {
            html += `
            <p class="error-suggestion">${t(suggestion)}</p>`;
        }

        return html;
    }

    /**
     * 生成文件过大错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.filename - 文件名
     * @param {number} info.filesize - 文件大小
     * @param {string} info.partname - 分区名称
     * @param {number} info.partsize - 分区大小
     * @returns {string} HTML字符串
     */
    function buildFileTooBigMessage(info) {
        return buildErrorTable(
            "error.file_too_big",
            [
                { label: "error.label.file_type", value: info.filename },
                { label: "error.label.file_size", value: bytesToHuman(info.filesize) + " (" + info.filesize + " " + t("error.unit.bytes") + ")" },
                { label: "error.label.part_name", value: info.partname },
                { label: "error.label.part_size", value: bytesToHuman(info.partsize) + " (" + info.partsize + " " + t("error.unit.bytes") + ")" }
            ],
            "error.suggestion.file_too_big"
        );
    }

    /**
     * 生成分区未找到错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.partname - 分区名称
     * @returns {string} HTML字符串
     */
    function buildPartNotFoundMessage(info) {
        return buildErrorTable(
            "error.part_not_found",
            [
                { label: "error.label.part_name", value: info.partname }
            ],
            "error.suggestion.part_not_found"
        );
    }

    /**
     * 生成文件类型错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.expected - 期望类型
     * @param {string} info.actual - 实际类型
     * @returns {string} HTML字符串
     */
    function buildWrongFileTypeMessage(info) {
        return buildErrorTable(
            "error.wrong_file_type",
            [
                { label: "error.label.expected_type", value: info.expected },
                { label: "error.label.actual_type", value: info.actual }
            ],
            "error.suggestion.wrong_file_type"
        );
    }

    /**
     * 生成闪存类型错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.filetype - 文件类型
     * @param {string} info.flashtype - 闪存类型
     * @returns {string} HTML字符串
     */
    function buildFlashNotFoundMessage(info) {
        return buildErrorTable(
            "error.flash_not_found",
            [
                { label: "error.label.file_type", value: info.filetype },
                { label: "error.label.flash_type", value: info.flashtype }
            ],
            "error.suggestion.flash_not_found"
        );
    }

    /**
     * 生成命令执行失败错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.cmd - 执行的命令
     * @param {string} info.output - 命令输出
     * @returns {string} HTML字符串
     */
    function buildRunCmdFailedMessage(info) {
        let html = `
            <div class="error-title">❌ ${t("error.run_cmd_failed")}</div>
            <div class="error-cmd-section">
                <div class="error-cmd-title">${t("error.label.cmd")}</div>
                <pre class="error-cmd-content">${escapeHtml(info.cmd || t("unknown"))}</pre>
            </div>
            <div class="error-output-section">
                <div class="error-output-title">${t("error.label.cmd_output")}</div>
                <pre class="error-output-content">${escapeHtml(info.output || t("error.no_output"))}</pre>
            </div>`;

        return html;
    }

    /**
     * 生成未知类型错误信息
     * @param {*} info - 错误信息（任意类型）
     * @returns {string} HTML字符串
     */
    function buildUnknownErrorMessage(info) {
        let message;
        try {
            message = JSON.stringify(info, null, 2);
        } catch (e) {
            message = String(info || t("error.unknown_type"));
        }

        return buildErrorTable(
            "error.title",
            [
                { label: "error.title", value: message }
            ]
        );
    }

    /**
     * 构建错误信息
     * @param {object} info - 错误信息对象
     * @param {string} info.type - 错误类型
     * @returns {string} HTML字符串
     */
    function buildErrorMessage(info) {
        const metadataCommitErrors = {
            stale_or_mismatched_commit: "metadata.error.stale_commit",
            staged_image_invalid: "metadata.error.staged_invalid",
            staged_bytes_changed: "metadata.error.bytes_changed",
            partition_layout_mismatch: "metadata.error.layout_changed",
            metadata_staging_unavailable: "metadata.error.staging_unavailable",
            metadata_write_or_readback_failed: "metadata.error.write_failed",
            owned_staging_validation_failed: "metadata.error.staging_failed",
            uboot_update_preflight_failed: "metadata.error.uboot_preflight",
            uboot_update_failed: "metadata.error.uboot_failed",
            uboot_update_partial: "metadata.error.uboot_partial",
            uboot_update_locked_after_partial: "metadata.error.uboot_locked",
            single_layout_needs_rootfs_selector_0: "metadata.error.single_needs_slot1"
        };
        const metadataErrorKey = info && metadataCommitErrors[info.type];
        if (metadataErrorKey) {
            let html = buildErrorTable("error.title", [
                { label: "error.title", value: t(metadataErrorKey) }
            ]);
            if (info.type === 'uboot_update_partial' ||
                    info.type === 'uboot_update_locked_after_partial') {
                html += '<p><a href="/uboot/backup/appsbl.bin" download="appsbl-before.bin">' +
                    escapeHtml(t('metadata.backup.primary')) + '</a> ' +
                    '<a href="/uboot/backup/appsbl_1.bin" download="appsbl_1-before.bin">' +
                    escapeHtml(t('metadata.backup.secondary')) + '</a></p>';
            }
            return html;
        }
        switch (info?.type) {
            case "file_too_big":
                return buildFileTooBigMessage(info);
            case "part_not_found":
                return buildPartNotFoundMessage(info);
            case "wrong_file_type":
                return buildWrongFileTypeMessage(info);
            case "flash_not_found":
                return buildFlashNotFoundMessage(info);
            case "run_cmd_failed":
                return buildRunCmdFailedMessage(info);
            default:
                return buildUnknownErrorMessage(info);
        }
    }

    function parseMetadataExtentValue(value, allowZero) {
        let number = value;
        if (typeof value === "string" && /^0x[0-9a-f]+$/i.test(value))
            number = Number(value);
        if (typeof number !== "number" || !Number.isSafeInteger(number) ||
            number < 0 || (!allowZero && number === 0))
            return null;
        return number;
    }

    function formatMetadataExtent(offsetValue, sizeValue) {
        const offset = parseMetadataExtentValue(offsetValue, true);
        const size = parseMetadataExtentValue(sizeValue, false);
        if (offset === null || size === null)
            return "";
        return " (0x" + offset.toString(16) + " + 0x" + size.toString(16) + ")";
    }

    function metadataTargetRows(info) {
        if (typeof info.target !== "string" || !info.target.length)
            return "";

        const targetNames = info.target.split(/\s*\+\s*/);
        const targetExtent = formatMetadataExtent(info.target_offset, info.target_size);
        const secondExtent = formatMetadataExtent(
            info.second_target_offset, info.second_target_size);
        const row = (labelKey, target, extent) => `
                    <tr>
                        <td class="info-label" data-i18n="${labelKey}">${t(labelKey)}</td>
                        <td class="info-value">${escapeHtml(target)}${extent}</td>
                    </tr>`;

        if (targetNames.length === 2 && targetNames[0] && targetNames[1] &&
            targetExtent && secondExtent) {
            return row("label.target", targetNames[0], targetExtent) +
                row("label.second_target", targetNames[1], secondExtent);
        }
        return row("label.target", info.target, targetExtent);
    }

    /** Build the success preview for a validated upload. */
    function buildSuccessMessage(info) {
        let html = `
            <table class="info-table">
                <tbody>`;

        if (info.name) {
            html += `
                    <tr>
                        <td class="info-label" data-i18n="label.name">${t("label.name")}</td>
                        <td class="info-value">${escapeHtml(info.name)}</td>
                    </tr>`;
        }

        if (info.type) {
            html += `
                    <tr>
                        <td class="info-label" data-i18n="label.type">${t("label.type")}</td>
                        <td class="info-value">${escapeHtml(info.type)}</td>
                    </tr>`;
        }

        if (info.size) {
            html += `
                    <tr>
                        <td class="info-label" data-i18n="label.size">${t("label.size")}</td>
                        <td class="info-value">${bytesToHuman(info.size)} (${info.size} Bytes)</td>
                    </tr>`;
        }

        if (info.md5) {
            html += `
                    <tr>
                        <td class="info-label" data-i18n="label.md5">${t("label.md5")}</td>
                        <td class="info-value upload-md5-value">${info.md5}</td>
                    </tr>`;
        }

        html += metadataTargetRows(info);
        html += `
                </tbody>
            </table>`;

        return html;
    }

    return {
        buildErrorMessage,
        buildSuccessMessage
    };
})();

// ==============================
// 统一的上传管理器
// ==============================

class UnifiedUploadManager {
    constructor() {
        this.instances = new Map();   // 存储每个页面的上传实例
        this.currentPage = null;
    }

    /**
     * 为指定页面初始化上传组件
     * @param {string} pageName - 页面名称
     * @param {object} options - 配置选项
     */
    initForPage(pageName, options = {}) {
        // 如果已经初始化过，先销毁
        if (this.instances.has(pageName)) {
            this.destroyPage(pageName);
        }

        // 获取容器
        const container = document.body;
        if (!container) {
            console.warn(`Body element not found for page: ${pageName}`);
            return null;
        }

        // 创建上传组件实例
        const instance = new FileUploadComponent(container, {
            formDataKey: options.formDataKey || pageName,
            warningItems: options.warningItems || null,
            pageName: pageName
        });

        // 存储实例
        this.instances.set(pageName, instance);

        // 渲染组件
        instance.render();

        return instance;
    }

    /**
     * 销毁指定页面的上传组件
     */
    destroyPage(pageName) {
        const instance = this.instances.get(pageName);
        if (instance) {
            instance.destroy();
            this.instances.delete(pageName);
        }
    }
}

// ==============================
// MIBIB 预设：原厂双系统 / 大分区
// ==============================

// The OEM table shared by every RT3000-family unit (both copies, ages 0/1,
// sha256 0b403b917823b515...). Rows: name, offset, size, then the user-table
// size and spare in KiB. The big layout merges rootfs and rootfs_1 into one
// 80 MiB rootfs at 0x900000; every other row stays the same.
const RT3_MIBIB_ROWS = [
    ['0:SBL1', 0x0, 0x80000, 0x100, 0x100],
    ['0:MIBIB', 0x80000, 0x80000, 0x100, 0x100],
    ['0:BOOTCONFIG', 0x100000, 0x40000, 0x80, 0x80],
    ['0:BOOTCONFIG1', 0x140000, 0x40000, 0x80, 0x80],
    ['0:QSEE', 0x180000, 0x100000, 0x200, 0x200],
    ['0:QSEE_1', 0x280000, 0x100000, 0x200, 0x200],
    ['0:DEVCFG', 0x380000, 0x40000, 0x80, 0x80],
    ['0:DEVCFG_1', 0x3c0000, 0x40000, 0x80, 0x80],
    ['0:CDT', 0x400000, 0x40000, 0x80, 0x80],
    ['0:CDT_1', 0x440000, 0x40000, 0x80, 0x80],
    ['0:APPSBLENV', 0x480000, 0x80000, 0x100, 0x100],
    ['0:APPSBL', 0x500000, 0x140000, 0x400, 0x100],
    ['0:APPSBL_1', 0x640000, 0x140000, 0x400, 0x100],
    ['0:ART', 0x780000, 0x100000, 0x200, 0x200],
    ['0:TRAINING', 0x880000, 0x80000, 0x100, 0x100],
    ['rootfs', 0x900000, 0x2800000, 0x9f00, 0x100],
    ['rootfs_1', 0x3100000, 0x2800000, 0x9f00, 0x100],
    ['pdt_data', 0x5900000, 0x600000, 0x1400, 0x400],
    ['pdt_data_1', 0x5f00000, 0x600000, 0x1400, 0x400],
    ['exp', 0x6500000, 0x100000, 0x200, 0x200],
    ['plugin', 0x6600000, 0x1700000, 0x5800, 0x400]
];

function rt3BuildMibib(single) {
    const BLOCK = 0x20000, PAGE = 0x800;
    const rows = RT3_MIBIB_ROWS
        .filter(r => !(single && r[0] === 'rootfs_1'))
        .map(r => (single && r[0] === 'rootfs') ? ['rootfs', 0x900000, 0x5000000, 0x13f00, 0x100] : r);
    const img = new Uint8Array(4 * BLOCK).fill(0xff);
    const dv = new DataView(img.buffer);
    const table = (at, magic1, magic2, attr, user) => {
        dv.setUint32(at, magic1, true);
        dv.setUint32(at + 4, magic2, true);
        dv.setUint32(at + 8, 4, true);
        dv.setUint32(at + 12, rows.length, true);
        img.fill(0, at + 16, at + 16 + 32 * 28);
        rows.forEach((r, i) => {
            const e = at + 16 + i * 28;
            for (let k = 0; k < r[0].length; k++) img[e + k] = r[0].charCodeAt(k);
            dv.setUint32(e + 16, user ? r[3] : r[1] / BLOCK, true);
            dv.setUint32(e + 20, user ? r[4] : r[2] / BLOCK, true);
            img.set(attr, e + 24);
        });
    };
    for (let copy = 0; copy < 2; copy++) {
        const b = copy * BLOCK;
        dv.setUint32(b, 0xfe569fac, true);
        dv.setUint32(b + 4, 0xcd7f127a, true);
        dv.setUint32(b + 8, 4, true);
        dv.setUint32(b + 12, copy, true);
        table(b + PAGE, 0x55ee73aa, 0xe35ebddb, [0xff, 0xff, 0x00, 0x00], false);
        table(b + 2 * PAGE, 0xaa7d1b9a, 0x1f7d48bc, [0xff, 0xff, 0x00, 0xff], true);
        // CRC-32, MSB first, poly 04C11DB7, init 0, over pages 0..2.
        let crc = 0;
        for (let i = b; i < b + 3 * PAGE; i++) {
            crc = (crc ^ (img[i] << 24)) >>> 0;
            for (let k = 0; k < 8; k++)
                crc = (crc & 0x80000000 ? (crc << 1) ^ 0x04c11db7 : crc << 1) >>> 0;
        }
        const end = b + 3 * PAGE;
        dv.setUint32(end, 0x9d41bea1, true);
        dv.setUint32(end + 4, 0xf1ded2ea, true);
        dv.setUint32(end + 8, 1, true);
        dv.setUint32(end + 12, crc, true);
    }
    return img;
}

// ==============================
// 文件上传组件类
// ==============================

class FileUploadComponent {
    constructor(container, options) {
        this.container = container;
        this.options = {
            formDataKey: 'file',
            warningItems: null,
            pageName: 'unknown',
            ...options
        };

        this.selectedFile = null;
        this.commitToken = null;
        this.commitAttempted = false;
        this.elements = {};
        this.isUploading = false;
    }

    /**
     * 渲染上传组件
     */
    render() {
        const pageName = this.options.pageName;
        const keys = this.getHintAndBtnKey();

        const html = `
            <div class="app">
                <aside id="sidebar" class="sidebar"></aside>
                <div class="sidebar-overlay" id="sidebarOverlay"></div>
                <button class="sidebar-toggle" id="sidebarToggle"></button>
                <div class="main">
                    <div id="m">
                        <div class="card-title-section">
                            <h1 id="title" data-i18n="${pageName}.title"></h1>
                        </div>

                        <div class="card-hint-section">
                            <p id="hint" data-i18n-html="${pageName}.hint"></p>
                        </div>

                        <div id="upload-container" class="upload-wrapper">
                            <div class="upload-card">
                                ${pageName === 'ptable' ? `<div class="card-hint-section">
                                    <p id="mibibLayoutNow"></p>
                                    <p>${APP_STATE.lang === 'zh-cn' ? '可直接生成分区表，无需自备文件：' : 'Generate a partition table here instead of choosing a file:'}</p>
                                    <button type="button" class="button" id="mibibPresetDual">${APP_STATE.lang === 'zh-cn' ? '原厂双系统' : 'OEM dual system'}</button>
                                    <button type="button" class="button" id="mibibPresetSingle">${APP_STATE.lang === 'zh-cn' ? '大分区（单 rootfs 80 MiB）' : 'Big partition (one 80 MiB rootfs)'}</button>
                                    <p>${APP_STATE.lang === 'zh-cn' ? '切换后，rootfs 区域里现有的系统都会失效：请先在“闪存备份”中导出 rootfs / rootfs_1。切换到大分区前，默认启动槽必须是物理第1区（0x900000）。大分区只接受第三方系统，原厂固件请切回原厂双系统分区表后再写。写入分区表后须重启，然后再写入系统。' : 'Switching invalidates every system in the rootfs area: export rootfs / rootfs_1 from Flash Backup first. Before switching to the big partition the default boot slot must be physical slot 1 (0x900000). The big partition accepts third-party systems only; restore the OEM dual table to write OEM firmware. Reboot after writing the table, then write a system.'}</p>
                                </div>` : ''}
                                ${pageName === 'cdt' ? `<div class="card-hint-section">
                                    <p>${APP_STATE.lang === 'zh-cn' ? 'CDT 与 CDT_1 两份写入同一镜像；请使用与本机硬件和内存配置匹配的 CDT。' : 'CDT and CDT_1 both receive the same image. Use a CDT matching this device and RAM configuration.'}</p>
                                </div>` : ''}
                                <div class="upload-area" id="uploadArea">
                                    <!-- 文件选择区域（正常状态） -->
                                    <div class="upload-selector" id="uploadSelector">
                                        <div class="drop-zone" id="dropZone">
                                            <div class="drop-zone-content">
                                                <svg class="drop-zone-icon" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                                    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                                                    <polyline points="17 8 12 3 7 8"/>
                                                    <line x1="12" y1="3" x2="12" y2="15"/>
                                                </svg>
                                                <p id="dropZoneText" class="drop-zone-text" data-i18n="file.dropzone.text"></p>
                                                <p class="drop-zone-hint" data-i18n="file.dropzone.hint"></p>
                                            </div>
                                        </div>

                                        <div class="upload-actions">
                                            <input type="file" id="fileInput" name="${this.options.formDataKey}" style="display: none;">
                                            <button type="button" id="uploadBtn" class="button" data-i18n="${keys.uploadBtn}"></button>
                                        </div>
                                    </div>

                                    <!-- 上传进度区域（上传时显示） -->
                                    <div class="upload-progress" id="uploadProgress" style="display: none;">
                                        <div class="progress-container">
                                            <div class="bar-circle" id="bar-circle" style="--percent: 0;"></div>
                                        </div>
                                        <p class="progress-status" id="progressStatus" data-i18n="file.uploading"></p>
                                    </div>

                                    <!-- 上传成功信息区域 -->
                                    <div class="upload-success" id="uploadSuccess" style="display: none;">
                                        <div id="upload-success-info"></div>
                                        <div class="continue-hint" data-i18n="${keys.continueHint}"></div>
                                        <div class="upload-success-actions">
                                            <button class="button" id="continueBtn" data-i18n="${keys.continueBtn}"></button>
                                            <button class="button" id="updateRebootBtn" data-i18n="common.update_reboot" style="display: none;"></button>
                                        </div>
                                    </div>

                                    <!-- 结果成功信息区域 -->
                                    <div class="result-success" id="resultSuccess" style="display: none;">
                                        <div id="result-success-info"></div>
                                    </div>

                                    <!-- 刷写/启动过程中的的加载动画 -->
                                    <div class="loading-spinner" id="loadingSpinner" style="display: none;"></div>

                                    <!-- 错误信息区域 -->
                                    <div class="error-area" id="errorArea" style="display: none;">
                                        <div id="error-info" class="error-message"></div>
                                    </div>
                                </div>
                            </div>
                        </div>

                        <div class="card-foot-section">
                            <div class="card-foot-inner">
                                <div class="i w">
                                    <strong data-i18n="common.warnings"></strong>
                                    <ul>${this.generateWarningList()}</ul>
                                </div>
                            </div>
                        </div>
                    </div>
                    <div id="version"></div>
                </div>
            </div>
        `;

        this.container.innerHTML = html;
        this.cacheElements();
        this.bindEvents();
        this.applyI18n();
    }

    /**
     * 生成页面底部的警告信息列表
     */
    generateWarningList() {
        const warningItems = this.options.warningItems;

        let warningList = '';

        if (warningItems) {
            if (warningItems.common) {
                for (let i = 1; i <= 1; i++) {
                    warningList += `<li data-i18n="common.warn.${i}"></li>`;
                }
            }
            if (warningItems.custom && warningItems.custom > 0) {
                for (let i = 1; i <= warningItems.custom; i++) {
                    warningList += `<li data-i18n="${this.options.pageName}.warn.${i}"></li>`;
                }
            }
        }

        return warningList;
    }

    /**
     * 获取上传按钮、继续按钮、继续提示的国际化 key
     */
    getHintAndBtnKey() {
        const keys = {};

        switch (this.options.pageName) {
            case 'initramfs' :
                keys.uploadBtn = 'common.upload';
                keys.continueHint = 'initramfs.boot_hint';
                keys.continueBtn = 'common.boot';
                break;
            case 'mibib' :
                keys.uploadBtn = 'mibib.reload';
                keys.continueHint = 'mibib.reload_success_hint';
                keys.continueBtn = 'mibib.reload_success';
                break;
            default:
                keys.uploadBtn = 'common.upload';
                keys.continueHint = 'common.upgrade_hint';
                keys.continueBtn = 'common.update';
                break;
        }

        return keys;
    }

    /**
     * 缓存 DOM 元素
     */
    cacheElements() {
        this.elements = {
            title: document.getElementById('title'),
            hint: document.getElementById('hint'),
            uploadArea: document.getElementById('uploadArea'),
            uploadSelector: document.getElementById('uploadSelector'),
            uploadProgress: document.getElementById('uploadProgress'),
            uploadSuccess: document.getElementById('uploadSuccess'),
            resultSuccess: document.getElementById('resultSuccess'),
            loadingSpinner: document.getElementById('loadingSpinner'),
            errorArea: document.getElementById('errorArea'),
            dropZone: document.getElementById('dropZone'),
            fileInput: document.getElementById('fileInput'),
            uploadBtn: document.getElementById('uploadBtn'),
            progressCircle: document.getElementById('bar-circle'),
            progressStatus: document.getElementById('progressStatus'),
            uploadSuccessInfo: document.getElementById('upload-success-info'),
            resultSuccessInfo: document.getElementById('result-success-info'),
            errorInfo: document.getElementById('error-info'),
            continueBtn: document.getElementById('continueBtn'),
            updateRebootBtn: document.getElementById('updateRebootBtn')
        };
    }

    /**
     * 绑定事件
     */
    bindEvents() {
        if (this.options.pageName === 'ptable') this.bindMibibPresets();
        // 拖拽区域点击
        if (this.elements.dropZone) {
            this.elements.dropZone.addEventListener('click', () => this.openFileSelector());
            this.bindDragEvents();
        }

        // 文件输入变化
        if (this.elements.fileInput) {
            this.elements.fileInput.addEventListener('change', (e) => {
                this.handleFileSelect(e.target.files[0]);
            });
        }

        // 上传按钮
        if (this.elements.uploadBtn) {
            this.elements.uploadBtn.addEventListener('click', () => {
                this.upload();
            });
        }

        // 继续按钮
        if (this.elements.continueBtn) {
            this.elements.continueBtn.addEventListener('click', () => {
                this.continue(false);
            });
        }

        // 更新并重启按钮
        if (this.elements.updateRebootBtn) {
            this.elements.updateRebootBtn.addEventListener('click', () => {
                this.continue(true);
            });
        }
    }

    /**
     * 绑定拖拽事件
     */
    bindDragEvents() {
        const dropZone = this.elements.dropZone;
        if (!dropZone) return;

        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
            dropZone.addEventListener(eventName, (e) => {
                e.preventDefault();
                e.stopPropagation();
            });
        });

        dropZone.addEventListener('dragenter', () => {
            dropZone.classList.add('drag-over');
        });

        dropZone.addEventListener('dragover', () => {
            dropZone.classList.add('drag-over');
        });

        dropZone.addEventListener('dragleave', () => {
            dropZone.classList.remove('drag-over');
        });

        dropZone.addEventListener('drop', (e) => {
            dropZone.classList.remove('drag-over');
            const files = e.dataTransfer.files;
            if (files && files.length > 0) {
                this.handleFileSelect(files[0]);
            }
        });
    }

    /**
     * 打开文件选择器
     */
    openFileSelector() {
        if (this.elements.fileInput && !this.isUploading) {
            this.elements.fileInput.click();
        }
    }

    bindMibibPresets() {
        const zh = APP_STATE.lang === 'zh-cn';
        const now = document.getElementById('mibibLayoutNow');
        if (now && typeof fetch === 'function') {
            fetch('/firmware/layout').then(r => r.json()).then(layout => {
                const names = { dual: zh ? '原厂双系统' : 'OEM dual system', single: zh ? '大分区' : 'big partition' };
                now.textContent = (zh ? '当前分区表：' : 'Current table: ') + (names[layout.mode] || layout.mode || '?');
            }).catch(() => {});
        }
        const presets = { mibibPresetDual: [false, 'mibib-oem-dual.bin'], mibibPresetSingle: [true, 'mibib-big-rootfs.bin'] };
        Object.keys(presets).forEach(id => {
            const button = document.getElementById(id);
            if (!button) return;
            button.addEventListener('click', () => {
                if (this.isUploading) return;
                const [single, name] = presets[id];
                this.handleFileSelect(new File([rt3BuildMibib(single)], name, { type: 'application/octet-stream' }));
            });
        });
    }

    /**
     * 处理文件选择
     */
    handleFileSelect(file) {
        this.commitToken = null;
        this.commitAttempted = false;
        this.selectedFile = file || null;
        if (!file) return;

        // 更新拖拽区域样式
        if (this.elements.dropZone) {
            this.elements.dropZone.classList.add('has-file');
        }

        // 更新显示文本
        this.updateDropZoneText(file.name);
    }

    /**
     * 更新拖拽区域文本
     */
    updateDropZoneText(filename) {
        const textElement = document.getElementById('dropZoneText');
        if (!textElement) return;

        if (filename) {
            const maxLength = 105;
            const displayName = filename.length > maxLength
                ? filename.substring(0, maxLength) + '...'
                : filename;
            textElement.innerHTML = `📄 ${escapeHtml(displayName)}`;
            textElement.style.fontWeight = '600';
            textElement.style.color = 'var(--primary)';
        } else {
            textElement.innerHTML = t('file.dropzone.text');
            textElement.style.fontWeight = '500';
            textElement.style.color = '';
        }
    }

    /**
     * 显示指定元素，隐藏其他无关元素
     */
    showElementAndHideOthers(id) {
        if (!id) return;

        const els = this.elements;
        const elements = [
            { id: 'uploadSelector', el: els.uploadSelector },
            { id: 'uploadProgress', el: els.uploadProgress },
            { id: 'uploadSuccess', el: els.uploadSuccess },
            { id: 'resultSuccess', el: els.resultSuccess },
            { id: 'loadingSpinner', el: els.loadingSpinner },
            { id: 'errorArea', el: els.errorArea }
        ];

        elements.forEach(element => {
            if (element.el) {
                element.el.style.display = element.id === id ? 'block' : 'none';
            }
        });
    }

    /**
     * 显示上传进度界面
     */
    showProgress() {
        this.showElementAndHideOthers('uploadProgress');
    }

    /**
     * 更新进度百分比
     */
    updateProgress(percent) {
        const p = Math.max(0, Math.min(100, percent));

        if (this.elements.progressCircle) {
            this.elements.progressCircle.style.setProperty('--percent', p);
        }

        if (this.elements.progressStatus) {
            if (p < 100) {
                this.elements.progressStatus.textContent = t('file.uploading');
            } else {
                this.elements.progressStatus.textContent = t('file.processing');
            }
        }
    }

    /**
     * 设置标题和提示文本
     * @param {string} titleKey - 标题国际化 key
     * @param {string} hintKey - 提示国际化 key
     */
    setTitleAndHint(titleKey, hintKey) {
        if (this.elements.title && titleKey) {
            this.elements.title.innerHTML = t(titleKey);
        }
        if (this.elements.hint && hintKey) {
            this.elements.hint.innerHTML = t(hintKey);
        }
    }

    /**
     * 显示成功界面
     */
    showSuccess(type, info) {
        const els = this.elements;
        const pageName = this.options.pageName;
        const isMibibPage = pageName === 'mibib';
        const isInitramfsPage = pageName === 'initramfs';

        this.isUploading = false;

        if (type === 'upload') {
            this.showElementAndHideOthers('uploadSuccess');
            if (els.uploadSuccessInfo && !isMibibPage) {
                els.uploadSuccessInfo.innerHTML = messageBuilder.buildSuccessMessage(info);
            }
            if (els.updateRebootBtn) {
                els.updateRebootBtn.style.display = (isMibibPage || isInitramfsPage) ? 'none' : 'inline-block';
            }
        } else {
            if (info.reboot) {
                this.showElementAndHideOthers('loadingSpinner');
            } else {
                this.showElementAndHideOthers('resultSuccess');
                if (els.resultSuccessInfo) {
                    els.resultSuccessInfo.innerHTML = t('flashing.msg.continue');
                }
            }
        }
    }

    /**
     * 显示错误界面
     */
    showError(type, info) {
        this.isUploading = false;

        if (type === 'upload') {
            this.setTitleAndHint('fail.title.validate', 'fail.hint.validate');
        } else {
            this.setTitleAndHint('fail.title.update', 'fail.hint.update');
        }

        this.showElementAndHideOthers('errorArea');

        if (this.elements.errorInfo) {
            this.elements.errorInfo.innerHTML = messageBuilder.buildErrorMessage(info);
        }
    }

    /**
     * 执行上传
     */
    upload() {
        if (this.options.pageName === 'mibib' && !is9008Mode()) {
            alert(t("mibib.not_9008"));
            return;
        }

        if (!this.selectedFile) {
            alert(t('file.select'));
            return;
        }

        if (this.isUploading) {
            alert(t('file.uploading'));
            return;
        }

        this.commitToken = null;
        this.commitAttempted = false;
        this.isUploading = true;
        this.showProgress();
        this.updateProgress(0);

        const uploadFile = this.selectedFile;
        const formData = new FormData();
        formData.append(this.options.formDataKey, uploadFile);

        const xhr = new XMLHttpRequest();

        xhr.upload.addEventListener('progress', (event) => {
            if (event.lengthComputable && event.total > 0) {
                const percent = parseInt((event.loaded / event.total) * 100);
                this.updateProgress(percent);
            }
        });

        xhr.onreadystatechange = () => {
            if (xhr.readyState === 4) {
                if (xhr.status === 200) {
                    if (this.selectedFile !== uploadFile) {
                        this.commitToken = null;
                        this.showError('upload', { type: 'invalid_response', message: 'Selected file changed during upload.' });
                        return;
                    }
                    let response;
                    try {
                        response = JSON.parse(xhr.responseText);
                    } catch (e) {
                        this.showError('upload', { type: 'invalid_response', message: xhr.responseText });
                        return;
                    }

                    switch (response.status) {
                        case 'success':
                            this.commitToken = null;
                            const expectedTargets = { uboot: "0:APPSBL + 0:APPSBL_1", art: "0:ART", cdt: "0:CDT + 0:CDT_1", ptable: "0:MIBIB" };
                            if (expectedTargets[this.options.pageName] && response.info &&
                                    [].concat(expectedTargets[this.options.pageName]).includes(response.info.target) &&
                                    typeof response.info.commit_token === "string" &&
                                    /^[0-9a-f]{64}$/.test(response.info.commit_token))
                                this.commitToken = response.info.commit_token;
                            if (this.commitToken) this.commitAttempted = false;
                            this.showSuccess('upload', response.info);
                            break;
                        case 'fail':
                            this.commitToken = null;
                            this.showError('upload', response.info);
                            break;
                        default:
                            this.commitToken = null;
                            this.showError('upload', { type: 'unknown_status', message: xhr.responseText });
                    }
                } else {
                    this.commitToken = null;
                    this.showError('upload', { type: 'http_error', message: `HTTP ${xhr.status}` });
                }
            }
        };

        const url = this.options.pageName === 'mibib' ? '/mibib/reload' : '/upload';
        xhr.open('POST', url);
        xhr.send(formData);
    }

    /**
     * 执行后续操作
     * @param {boolean} autoRebootAfterSuccess - 操作成功后是否自动重启（仅对刷写更新类操作有效）
     */
    continue(autoRebootAfterSuccess) {
        if (this.commitAttempted && !this.commitToken) return;
        const pageName = this.options.pageName;
        const isMibibPage = pageName === 'mibib';
        const isInitramfsPage = pageName === 'initramfs';
        const family = typeof APP_STATE !== 'undefined' && APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.family;
        if (['HW-A', 'HW-B', 'HW-C', 'HW-D'].includes(family) && ['uboot', 'art', 'cdt', 'ptable'].includes(pageName) && !this.commitToken) return;

        if (isMibibPage) {
            window.location.href = '/';
            return;
        }

        const action = isInitramfsPage ? 'booting' : 'flashing';
        const titleStart = action + '.title.in_progress';
        const hintStart = action + '.hint.in_progress';
        const titleDone = action + '.title.done';
        const hintDone = action + '.hint.done';
        const hintContinue = 'flashing.hint.continue';

        this.setTitleAndHint(titleStart, hintStart);
        this.showElementAndHideOthers('loadingSpinner');

        const formData = new FormData();
        formData.append('auto_reboot', autoRebootAfterSuccess ? 'true' : 'false');
        const commitToken = this.commitToken;
        const commitMetadata = !!commitToken && ["uboot", "art", "cdt", "ptable"].includes(pageName);
        if (commitMetadata) {
            formData.append('commit_token', commitToken);
            this.commitToken = null;
            this.commitAttempted = true;
        }

        const xhr = new XMLHttpRequest();

        xhr.timeout = 600000; // 10分钟

        xhr.onreadystatechange = () => {
            if (xhr.readyState === 4) {
                if (xhr.status === 200) {
                    let response;
                    try {
                        response = JSON.parse(xhr.responseText);
                    } catch (e) {
                        this.showError('result', { type: 'invalid_response', message: xhr.responseText });
                        return;
                    }

                    switch (response.status) {
                        case 'success':
                            this.setTitleAndHint(titleDone, response.info.reboot ? hintDone : hintContinue);
                            this.showSuccess('result', response.info);
                            break;
                        case 'fail':
                            this.showError('result', response.info);
                            break;
                        default:
                            this.showError('result', { type: 'unknown_status', message: xhr.responseText });
                    }
                } else {
                    this.showError('result', { type: 'http_error', message: `HTTP ${xhr.status}` });
                }
            }
        };

        xhr.open('POST', commitMetadata ? '/write' : '/result');
        xhr.send(formData);
    }

    /**
     * 应用国际化
     */
    applyI18n() {
        if (typeof applyI18n === 'function') {
            applyI18n(this.container);
        }
    }

    /**
     * 销毁组件
     */
    destroy() {
        if (this.container) {
            this.container.innerHTML = '';
        }
        this.elements = {};
        this.selectedFile = null;
        this.commitToken = null;
        this.commitAttempted = false;
        this.isUploading = false;
    }
}

// 全局统一上传管理器实例
let unifiedUploadManager = null;

/**
 * 获取统一上传管理器实例
 */
function getUnifiedUploadManager() {
    if (!unifiedUploadManager) {
        unifiedUploadManager = new UnifiedUploadManager();
    }
    return unifiedUploadManager;
}

// ==============================
// 备份功能模块
// ==============================

/**
 * 备份管理器
 * 负责处理闪存备份的配置、条目选择、批量下载等功能
 */
const backupManager = (() => {
    let elements = null;
    let targetsLoaded = false;
    let rawDevicesData = {};     // 存储原始设备数据
    let smemPartsData = [];      // 存储 SMEM 分区数据
    let mmcPartsData = [];       // 存储 MMC 分区数据
    let itemsData = [];          // 当前显示的条目列表（full 模式）
    let selectedItems = new Set(); // 选中的条目索引（full 模式）
    let rangeItemsData = [];     // range 模式下拉选择器的条目列表
    let rangeSelectedValue = ""; // range 模式选中的值
    let isDownloading = false;
    let totalDownloadSize = 0;
    let downloadedSize = 0;
    let progressItemsMap = new Map();
    const MAX_AUTO_SELECT_SIZE = 256 * 1024 * 1024; // 256 MiB

    // 输入验证状态
    let validationState = {
        start: { valid: true, value: null, message: '' },
        size: { valid: true, value: null, message: '' }
    };

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            mode: document.getElementById("backup_mode"),
            target: document.getElementById("backup_target_category"),
            range: document.getElementById("backup_range"),
            rangeSelect: document.getElementById("backup_range_select"),
            start: document.getElementById("backup_start"),
            size: document.getElementById("backup_size"),
            rangeHint: document.getElementById("backup_range_hint"),
            itemsContainer: document.getElementById("backup_items_container"),
            itemsScroll: document.getElementById("backup_items_scroll"),
            itemsList: document.getElementById("backup_items_list"),
            itemsCount: document.getElementById("backup_items_count"),
            progressContainer: document.getElementById("backup_progress_container"),
            progressScroll: document.getElementById("backup_progress_scroll"),
            progressList: document.getElementById("backup_progress_list"),
            progressStatus: document.getElementById("backup_progress_status"),
            progressPercent: document.getElementById("backup_progress_percent"),
            progressBarFill: document.getElementById("backup_progress_bar_fill"),
            progressBarText: document.getElementById("backup_progress_bar_text"),
        };

        return elements;
    }

    /**
     * 格式化字节数为可读字符串
     */
    function bytesToHuman(bytes) {
        if (bytes === null || bytes === undefined) return "未知";
        const numBytes = Number(bytes);
        if (!isFinite(numBytes) || numBytes < 0) return "未知";
        if (numBytes >= 1024 * 1024 * 1024) {
            return (numBytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB";
        } else if (numBytes >= 1024 * 1024) {
            return (numBytes / (1024 * 1024)).toFixed(2) + " MiB";
        } else if (numBytes >= 1024) {
            return (numBytes / 1024).toFixed(2) + " KiB";
        } else {
            return String(Math.floor(numBytes)) + " B";
        }
    }

    /**
     * 获取条目的大小（以字节为单位）
     */
    function getItemSize(item) {
        if (item.size !== null && item.size !== undefined && typeof item.size === 'number') {
            return item.size;
        }
        return 0;
    }

    /**
     * 检查条目是否超过自动选择阈值（MAX_AUTO_SELECT_SIZE）
     */
    function isLargeItem(item) {
        return getItemSize(item) > MAX_AUTO_SELECT_SIZE;
    }

    /**
     * 获取当前选中的条目（range 模式下从下拉选择器获取）
     */
    function getSelectedRangeItem() {
        if (!rangeSelectedValue) return null;
        return rangeItemsData.find(item => item.id === rangeSelectedValue) || null;
    }

    /**
     * 检查用户是否输入起始偏移
     */
    function hasStartInput() {
        const els = getElements();
        const startInput = els.start ? els.start.value.trim() : '';
        return startInput !== '';
    }

    /**
     * 检查用户是否输入大小
     */
    function hasSizeInput() {
        const els = getElements();
        const sizeInput = els.size ? els.size.value.trim() : '';
        return sizeInput !== '';
    }

    /**
     * 解析用户输入的长度（支持十六进制和K/M后缀）
     * @param {string} str - 用户输入的字符串
     * @returns {object|null} { value: 字节数, raw: 原始数值, unit: 单位 }
     * 注：该函数修改自项目：Yuzhii0718/bl-mt798x-dhcpd
     */
    function parseUserLen(str) {
        if (!str) return null;
        str = String(str).trim();
        if (str === "") return null;

        const match = /^\s*(0x[0-9a-fA-F]+|\d+)\s*([a-zA-Z]*)\s*$/.exec(str);
        if (!match) return null;

        let val = match[1].toLowerCase().indexOf("0x") === 0 ?
                  parseInt(match[1], 16) : parseInt(match[1], 10);
        if (!isFinite(val) || val < 0) return null;

        const suffix = (match[2] || "").toLowerCase();
        let bytes = val;
        let unit = suffix || "B";

        if (suffix === "" || suffix === "b") {
            bytes = val;
            unit = "B";
        } else if (suffix === "k" || suffix === "kb" || suffix === "kib") {
            bytes = val * 1024;
            unit = "KiB";
        } else if (suffix === "m" || suffix === "mb" || suffix === "mib") {
            bytes = val * 1024 * 1024;
            unit = "MiB";
        } else {
            return null;
        }

        return { value: bytes, raw: val, unit: unit };
    }

    /**
     * 验证 range 模式的输入
     * @returns {object} { valid: boolean, start: number|null, size: number|null, errors: array }
     */
    function validateRangeInput() {
        const els = getElements();
        if (els.mode?.value !== 'range') {
            return;
        }

        const useChinese = APP_STATE.lang && APP_STATE.lang === 'zh-cn';
        const startStr = els.start?.value || '';
        const sizeStr = els.size?.value || '';
        const errors = [];
        let startValue = null;
        let sizeValue = null;

        // 获取当前选中的条目（从下拉选择器）
        const selectedItem = getSelectedRangeItem();
        const itemSize = selectedItem ? getItemSize(selectedItem) : 0;

        // 解析起始地址
        const startResult = parseUserLen(startStr);
        if (startResult === null) {
            const message = hasStartInput()
                ? t('backup.range.error.invalid_start')
                : t('backup.range.error.empty_start');
            errors.push({ field: 'start', message: message });
            validationState.start = { valid: false, value: null, message: message };
        } else {
            startValue = startResult.value;
            if (startValue >= itemSize) {
                const message = useChinese
                    ? '起始地址超出条目大小范围（最大 ' + bytesToHuman(itemSize) + '）'
                    : 'Start address exceeds item size (max ' + bytesToHuman(itemSize) + ')';
                errors.push({ field: 'start', message: message });
                validationState.start = { valid: false, value: startValue, message: message };
            } else {
                validationState.start = { valid: true, value: startValue, message: '' };
            }
        }

        // 解析大小
        const sizeResult = parseUserLen(sizeStr);
        if (sizeResult === null) {
            const message = hasSizeInput()
                ? t('backup.range.error.invalid_size')
                : t('backup.range.error.empty_size');
            errors.push({ field: 'size', message: message });
            validationState.size = { valid: false, value: null, message: message };
        } else if (sizeResult.value <= 0) {
            const message = t('backup.range.error.size_zero');
            errors.push({ field: 'size', message: message });
            validationState.size = { valid: false, value: null, message: message };
        } else {
            sizeValue = sizeResult.value;
            validationState.size = { valid: true, value: sizeValue, message: '' };
        }

        // 如果两个都有效，检查是否在条目范围内
        if (startValue !== null && sizeValue !== null && selectedItem) {
            if (itemSize > 0) {
                // 检查起始地址 + 大小是否超出范围
                if (startValue + sizeValue > itemSize) {
                    const message = useChinese
                        ? '起始地址 + 大小超出条目大小范围（最大 ' + bytesToHuman(itemSize) + '）'
                        : 'Start + size exceeds item size (max ' + bytesToHuman(itemSize) + ')';
                    errors.push({ field: 'size', message: message });
                    validationState.size = { valid: false, value: sizeValue, message: message };
                }

                // 检查起始地址 + 大小是否等于条目大小（允许等于，表示备份到末尾）
                // 这是允许的，不需要报错
            }
        }

        // 更新输入框样式
        updateInputValidation();

        return {
            valid: errors.length === 0,
            start: startValue,
            size: sizeValue,
            itemSize: itemSize,
            errors: errors
        };
    }

    /**
     * 更新输入框的验证样式
     */
    function updateInputValidation() {
        const els = getElements();

        // 更新起始地址输入框
        if (els.start) {
            els.start.classList.remove('input-valid', 'input-invalid');
            if (validationState.start.valid || !hasStartInput()) {
                els.start.classList.add('input-valid');
            } else {
                els.start.classList.add('input-invalid');
            }
        }

        // 更新大小输入框
        if (els.size) {
            els.size.classList.remove('input-valid', 'input-invalid');
            if (validationState.size.valid || !hasSizeInput()) {
                els.size.classList.add('input-valid');
            } else {
                els.size.classList.add('input-invalid');
            }
        }

        // 更新提示信息
        updateRangeHint();
    }

    /**
     * 更新范围提示
     */
    function updateRangeHint() {
        const els = getElements();
        const hint = els.rangeHint;
        if (!hint) return;

        const startState = validationState.start;
        const sizeState = validationState.size;
        const startValid = startState.valid;
        const sizeValid = sizeState.valid;
        const defaultHint = t("backup.range.hint");

        if (!hasStartInput() && !hasSizeInput()) {
            hint.textContent = defaultHint;
            hint.style.color = '';
            return;
        }

        // 如果有错误信息，显示错误
        if (!startValid) {
            hint.textContent = '❌ ' + startState.message;
            hint.style.color = 'var(--danger)';
            return;
        }
        if (!sizeValid) {
            hint.textContent = '❌ ' + sizeState.message;
            hint.style.color = 'var(--danger)';
            return;
        }

        // 如果两个都有效，显示范围信息
        // 注：此部分修改自项目：Yuzhii0718/bl-mt798x-dhcpd
        if (startValid && sizeValid) {
            const startVal = startState.value;
            const sizeVal = sizeState.value;
            const end = startVal + sizeVal - 1;

            hint.textContent = "Start = " + startVal + " B (" + bytesToHuman(startVal) + ")" +
                              ", Size = " + sizeVal + " B (" + bytesToHuman(sizeVal) + ")" +
                              ", End = " + end + " B (" + bytesToHuman(end) + ")";
            hint.style.color = 'var(--primary)';

            return;
        }

        hint.textContent = defaultHint;
        hint.style.color = '';
    }

    /**
     * 重置输入验证状态
     */
    function resetValidation() {
        validationState.start = { valid: true, value: null, message: '' };
        validationState.size = { valid: true, value: null, message: '' };
        const els = getElements();
        if (els.start) {
            els.start.classList.remove('input-valid', 'input-invalid');
        }
        if (els.size) {
            els.size.classList.remove('input-valid', 'input-invalid');
        }
        updateRangeHint();
    }

    /**
     * 解析Content-Disposition中的文件名
     * 注：该函数来自项目：Yuzhii0718/bl-mt798x-dhcpd
     */
    function parseFilenameFromDisposition(header) {
        if (!header) return "";
        const match = /filename\s*=\s*"([^"]+)"/i.exec(header);
        if (match && match[1]) return match[1];
        const match2 = /filename\s*=\s*([^;\s]+)/i.exec(header);
        if (match2 && match2[1]) return match2[1].replace(/^"|"$/g, "");
        return "";
    }

    /**
     * 生成安全的文件名
     */
    function generateSafeFilename(target, index, total) {
        const now = new Date();
        const timestamp = now.getFullYear() +
            String(now.getMonth() + 1).padStart(2, '0') +
            String(now.getDate()).padStart(2, '0') + '_' +
            String(now.getHours()).padStart(2, '0') +
            String(now.getMinutes()).padStart(2, '0') +
            String(now.getSeconds()).padStart(2, '0');

        let name = target.replace(/^raw:/, 'RAW_')
                         .replace(/^smem:/, 'SMEM_')
                         .replace(/^mmc:/, 'MMC_')
                         .replace(/[^a-zA-Z0-9_\-]/g, '_');

        if (total > 1) {
            return `${timestamp}_${name}_${index + 1}of${total}.bin`;
        }
        return `${timestamp}_${name}.bin`;
    }

    /**
     * 初始化条目选中状态（默认勾选不超过 MAX_AUTO_SELECT_SIZE 的条目）
     */
    function initSelection() {
        selectedItems.clear();
        itemsData.forEach(function(item, index) {
            if (!isLargeItem(item)) {
                selectedItems.add(index);
            }
        });
    }

    /**
     * 加载目标设备列表
     */
    function loadTargets() {
        const els = getElements();
        if (!els.target) return;

        ajax({
            url: "/sysinfo",
            timeout: 10000,
            done: function(text) {
                let info;
                try {
                    info = JSON.parse(text);
                } catch (e) {
                    showError(t("backup.error.exception") + " " + t("sysinfo.error.parse"));
                    return;
                }

                els.target.innerHTML = '<option value="" data-i18n="backup.target.placeholder"></option>';

                rawDevicesData = {};
                smemPartsData = [];
                mmcPartsData = [];

                let hasRawDevice = false;
                if (info.devices) {
                    const rawTypes = ['spi', 'mmc', 'nand'];
                    rawTypes.forEach(function(key) {
                        const device = info.devices[key];
                        if (device && device.present) {
                            rawDevicesData[key] = device;
                            hasRawDevice = true;
                        }
                    });
                }

                if (hasRawDevice) {
                    const opt = document.createElement("option");
                    opt.value = "raw";
                    opt.textContent = t('backup.target.raw');
                    opt.dataset.category = "raw";
                    els.target.appendChild(opt);
                }

                if (info.partitions && info.partitions.smem && info.partitions.smem.present) {
                    smemPartsData = info.partitions.smem.parts || [];
                    const opt = document.createElement("option");
                    opt.value = "smem";
                    opt.textContent = t('backup.target.smem');
                    opt.dataset.category = "smem";
                    els.target.appendChild(opt);
                }

                if (info.partitions && info.partitions.mmc && info.partitions.mmc.present) {
                    mmcPartsData = info.partitions.mmc.parts || [];
                    const opt = document.createElement("option");
                    opt.value = "mmc";
                    opt.textContent = t('backup.target.mmc');
                    opt.dataset.category = "mmc";
                    els.target.appendChild(opt);
                }

                applyI18n(els.target);
                targetsLoaded = true;

                if (els.target.options.length > 1) {
                    els.target.selectedIndex = 1;
                    onTargetChange();
                }

                els.target.addEventListener("change", onTargetChange);
            }
        });
    }

    /**
     * 目标选择变更处理
     */
    function onTargetChange() {
        const els = getElements();
        const selectedOption = els.target.options[els.target.selectedIndex];

        if (!selectedOption || !selectedOption.value) {
            els.itemsContainer.style.display = "none";
            els.range.style.display = "none";
            itemsData = [];
            rangeItemsData = [];
            selectedItems.clear();
            rangeSelectedValue = "";
            renderItems();
            renderRangeSelect();
            updateModeUI();
            resetValidation();
            validateRangeInput();
            return;
        }

        const category = selectedOption.dataset.category;
        itemsData = [];
        rangeItemsData = [];
        selectedItems.clear();
        rangeSelectedValue = "";

        if (category === 'raw') {
            const typeLabels = {
                'spi': 'SPI',
                'mmc': 'MMC',
                'nand': 'NAND'
            };

            Object.keys(rawDevicesData).forEach(function(key) {
                const device = rawDevicesData[key];
                if (device) {
                    const label = '[RAW] ' + (typeLabels[key] || key.toUpperCase()) +
                                    ': ' + (device.name || device.product || '');
                    const item = {
                        id: 'raw:' + key,
                        type: 'raw',
                        name: label,
                        device: key,
                        size: device.size || null,
                        display: label + (device.size ? ' (' + bytesToHuman(device.size) + ')' : '')
                    };
                    itemsData.push(item);
                    rangeItemsData.push(item);
                }
            });
        } else if (category === 'smem') {
            smemPartsData.forEach(function(part) {
                if (part && part.name) {
                    const item = {
                        id: 'smem:' + part.name,
                        type: 'smem',
                        name: part.name,
                        size: part.size || 0,
                        display: '[SMEM] ' + part.name +
                                (part.size ? ' (' + bytesToHuman(part.size) + ')' : '')
                    };
                    itemsData.push(item);
                    rangeItemsData.push(item);
                }
            });
        } else if (category === 'mmc') {
            mmcPartsData.forEach(function(part) {
                if (part && part.name) {
                    const item = {
                        id: 'mmc:' + part.name,
                        type: 'mmc',
                        name: part.name,
                        size: part.size || 0,
                        display: '[MMC] ' + part.name +
                                (part.size ? ' (' + bytesToHuman(part.size) + ')' : '')
                    };
                    itemsData.push(item);
                    rangeItemsData.push(item);
                }
            });
        }

        // 如果 range 有数据，默认选中第一个
        if (rangeItemsData.length > 0) {
            rangeSelectedValue = rangeItemsData[0].id;
        }

        initSelection();
        renderItems();
        renderRangeSelect();
        updateItemsCount();
        updateModeUI();
        resetValidation();
        validateRangeInput();

        if (els.mode.value === 'full' && itemsData.length > 0) {
            els.itemsContainer.style.display = "block";
        } else {
            els.itemsContainer.style.display = "none";
        }

        if (els.mode.value === 'range' && rangeItemsData.length > 0) {
            els.range.style.display = "block";
        } else {
            els.range.style.display = "none";
        }
    }

    /**
     * 渲染条目列表（full 模式）
     */
    function renderItems() {
        const els = getElements();
        const list = els.itemsList;
        if (!list) return;

        if (itemsData.length === 0) {
            list.innerHTML = '<div class="backup-items-empty" data-i18n="backup.items.empty"></div>';
            return;
        }

        let html = '';
        itemsData.forEach(function(item, index) {
            const checked = selectedItems.has(index) ? 'checked' : '';
            const isLarge = isLargeItem(item);
            const largeClass = isLarge ? ' backup-item-large' : '';

            html += `
                <div class="backup-item${largeClass}" data-index="${index}">
                    <label class="backup-item-label">
                        <input type="checkbox" class="backup-item-checkbox" ${checked} data-index="${index}">
                        <span class="backup-item-name">${escapeHtml(item.display)}</span>
                        ${isLarge ? '<span class="backup-item-badge" data-i18n="backup.badge.large"></span>' : ''}
                    </label>
                </div>
            `;
        });

        list.innerHTML = html;
        applyI18n(list);

        list.querySelectorAll('.backup-item-checkbox').forEach(function(cb) {
            cb.addEventListener('change', function() {
                const index = parseInt(this.dataset.index);
                if (this.checked) {
                    selectedItems.add(index);
                } else {
                    selectedItems.delete(index);
                }
                updateItemsCount();
                updateModeUI();
            });
        });
    }

    /**
     * 渲染 range 模式下拉选择器
     */
    function renderRangeSelect() {
        const els = getElements();
        const select = els.rangeSelect;
        if (!select) return;

        // 清空选择器
        select.innerHTML = '';

        if (rangeItemsData.length === 0) {
            const opt = document.createElement('option');
            opt.value = '';
            opt.textContent = '-- ' + t('backup.items.empty') + ' --';
            select.appendChild(opt);
            return;
        }

        // 添加选项
        rangeItemsData.forEach(function(item) {
            const opt = document.createElement('option');
            opt.value = item.id;
            opt.textContent = item.display;
            if (item.id === rangeSelectedValue) {
                opt.selected = true;
            }
            select.appendChild(opt);
        });

        // 绑定 change 事件
        select.addEventListener('change', function() {
            rangeSelectedValue = this.value;
            validateRangeInput();
        });
    }

    /**
     * 更新条目计数
     */
    function updateItemsCount() {
        const els = getElements();
        if (els.itemsCount) {
            const total = itemsData.length;
            const selected = selectedItems.size;
            const largeCount = itemsData.filter(function(item) { return isLargeItem(item); }).length;
            let largeInfo = '';
            if (largeCount > 0) {
                largeInfo = ` (${largeCount} ` + t('backup.badge.large') + ')';
            }
            els.itemsCount.textContent = t('backup.items.count') + ': ' + selected + ' / ' + total + largeInfo;
        }
    }

    /**
     * 全选（跳过超过 MAX_AUTO_SELECT_SIZE 的条目）
     */
    function selectAll() {
        selectedItems.clear();
        itemsData.forEach(function(item, index) {
            if (!isLargeItem(item)) {
                selectedItems.add(index);
            }
        });
        renderItems();
        updateItemsCount();
        updateModeUI();
    }

    /**
     * 全选（包括大文件）
     */
    function selectAllForce() {
        selectedItems.clear();
        itemsData.forEach(function(_, index) {
            selectedItems.add(index);
        });
        renderItems();
        updateItemsCount();
        updateModeUI();
    }

    /**
     * 取消全选
     */
    function deselectAll() {
        selectedItems.clear();
        renderItems();
        updateItemsCount();
        updateModeUI();
    }

    /**
     * 添加进度条目（不自动滚动到底部）
     */
    function addProgressItem(index, total, name, itemSize) {
        const els = getElements();
        const list = els.progressList;
        if (!list) return;

        progressItemsMap.set(index, {
            total: itemSize || 0,
            downloaded: 0
        });

        if (itemSize && itemSize > 0) {
            totalDownloadSize += itemSize;
        }

        const div = document.createElement('div');
        div.className = 'backup-progress-item';
        div.dataset.index = index;
        div.innerHTML = `
            <span class="backup-progress-item-name">${escapeHtml(name)}</span>
            <span class="backup-progress-item-status" data-status="pending">${t('backup.status.pending')}</span>
            <span class="backup-progress-item-size"></span>
            <span class="backup-progress-item-percent">0%</span>
        `;
        list.appendChild(div);

        updateOverallProgress();
    }

    /**
     * 更新进度条目状态（不自动滚动到底部）
     */
    function updateProgressItem(index, status, message, size, percent) {
        const els = getElements();
        const list = els.progressList;
        if (!list) return;

        const item = list.querySelector(`.backup-progress-item[data-index="${index}"]`);
        if (!item) return;

        const statusEl = item.querySelector('.backup-progress-item-status');
        const sizeEl = item.querySelector('.backup-progress-item-size');
        const percentEl = item.querySelector('.backup-progress-item-percent');

        const itemData = progressItemsMap.get(index);
        if (itemData) {
            if (size !== undefined && size > 0) {
                const newDownloaded = size;
                const delta = newDownloaded - itemData.downloaded;
                itemData.downloaded = newDownloaded;

                if (itemData.total > 0 && delta > 0) {
                    downloadedSize += delta;
                }
            }
        }

        if (statusEl) {
            statusEl.dataset.status = status;
            statusEl.textContent = t('backup.status.' + status);
        }

        if (sizeEl && size !== undefined) {
            sizeEl.textContent = bytesToHuman(size);
        }

        if (percentEl && percent !== undefined) {
            percentEl.textContent = Math.round(percent) + '%';
        }

        if (status === 'done') {
            item.classList.add('done');
            const itemData2 = progressItemsMap.get(index);
            if (itemData2 && itemData2.total > 0) {
                if (itemData2.downloaded < itemData2.total) {
                    const delta = itemData2.total - itemData2.downloaded;
                    downloadedSize += delta;
                    itemData2.downloaded = itemData2.total;
                }
            }
        } else if (status === 'error') {
            item.classList.add('error');
        }

        updateOverallProgress();
    }

    /**
     * 更新总体进度
     */
    function updateOverallProgress() {
        const els = getElements();
        const items = els.progressList?.querySelectorAll('.backup-progress-item') || [];

        if (items.length === 0) return;

        let done = 0;
        let total = items.length;

        items.forEach(function(item) {
            const status = item.querySelector('.backup-progress-item-status')?.dataset.status;
            if (status === 'done' || status === 'error') {
                done++;
            }
        });

        let percent = 0;
        if (totalDownloadSize > 0) {
            percent = Math.round((downloadedSize / totalDownloadSize) * 100);
            if (percent > 100) percent = 100;
        } else {
            percent = total > 0 ? Math.round((done / total) * 100) : 0;
        }

        if (els.progressPercent) {
            els.progressPercent.textContent = percent + '%';
        }

        if (els.progressBarFill) {
            els.progressBarFill.style.width = percent + '%';
        }

        if (els.progressBarText) {
            if (totalDownloadSize > 0 && downloadedSize > 0) {
                const downloadedStr = bytesToHuman(downloadedSize);
                const totalStr = bytesToHuman(totalDownloadSize);
                els.progressBarText.textContent = downloadedStr + ' / ' + totalStr;
            } else {
                els.progressBarText.textContent = done + ' / ' + total;
            }
        }
    }

    /**
     * 更新总体进度状态
     */
    function updateOverallStatus(text) {
        const els = getElements();
        if (els.progressStatus) {
            els.progressStatus.textContent = text;
        }
    }

    /**
     * 显示进度区域
     */
    function showProgressArea() {
        const els = getElements();
        els.progressContainer.style.display = 'block';
        els.progressList.innerHTML = '';
        updateOverallStatus('');
        updateOverallProgress();
        // 滚动到进度区域顶部，方便用户看到所有进度
        if (els.progressContainer) {
            els.progressContainer.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    }

    /**
     * 隐藏进度区域
     */
    function hideProgressArea() {
        const els = getElements();
        els.progressContainer.style.display = 'none';
        els.progressList.innerHTML = '';
    }

    /**
     * 显示错误信息
     */
    function showError(text) {
        const els = getElements();
        if (els.progressContainer) {
            els.progressContainer.style.display = 'block';
            els.progressList.innerHTML = '';
            updateOverallStatus('❌ ' + text);
        }
    }

    /**
     * 更新UI模式显示
     */
    function updateModeUI() {
        const els = getElements();
        const rangeEl = els.range;
        const itemsContainer = els.itemsContainer;
        const currentMode = els.mode?.value;

        // 处理 range 模式显示
        if (rangeEl) {
            if (currentMode === 'range' && rangeItemsData.length > 0) {
                rangeEl.style.display = 'block';
                validateRangeInput();
            } else {
                rangeEl.style.display = 'none';
            }
        }

        // 处理 full 模式显示
        if (itemsContainer) {
            if (currentMode === 'full' && itemsData.length > 0) {
                itemsContainer.style.display = 'block';
            } else {
                itemsContainer.style.display = 'none';
            }
        }
    }

    /**
     * 处理模式切换事件
     */
    function onModeChange() {
        const els = getElements();
        const newMode = els.mode?.value;

        // 如果切换到 range 模式，重新验证
        if (newMode === 'range') {
            resetValidation();
            validateRangeInput();
        }

        updateModeUI();
    }

    /**
     * 下载单个条目
     */
    function downloadItem(item, index, total) {
        return new Promise((resolve, reject) => {
            const formData = new FormData();
            formData.append("mode", "full");
            formData.append("target", item.id);

            updateProgressItem(index, 'downloading', '', 0, 0);

            const xhr = new XMLHttpRequest();
            const chunks = [];
            let totalSize = 0;
            let receivedSize = 0;

            xhr.open("POST", "/backup", true);
            xhr.responseType = 'arraybuffer';

            xhr.onprogress = function(event) {
                if (event.lengthComputable) {
                    totalSize = event.total;
                    receivedSize = event.loaded;
                    const percent = totalSize > 0 ? Math.round((receivedSize / totalSize) * 100) : 0;
                    updateProgressItem(index, 'downloading', '', receivedSize, percent);
                }
            };

            xhr.onreadystatechange = function() {
                if (xhr.readyState === 3 || xhr.readyState === 4) {
                    try {
                        if (xhr.response && xhr.response.byteLength > 0) {
                            if (!xhr._lastResponseSize || xhr._lastResponseSize !== xhr.response.byteLength) {
                                const chunk = new Uint8Array(xhr.response);
                                chunks.push(chunk);
                                xhr._lastResponseSize = xhr.response.byteLength;

                                if (!totalSize) {
                                    receivedSize += chunk.length;
                                    updateProgressItem(index, 'downloading', '', receivedSize, 0);
                                }
                            }
                        }
                    } catch (e) {
                        // ignore
                    }
                }

                if (xhr.readyState === 4) {
                    if (xhr.status === 200) {
                        try {
                            let totalLength = 0;
                            const chunksCopy = chunks.slice();
                            chunksCopy.forEach(function(chunk) {
                                totalLength += chunk.length;
                            });

                            if (totalLength === 0) {
                                if (xhr.response && xhr.response.byteLength > 0) {
                                    const directData = new Uint8Array(xhr.response);
                                    chunksCopy.push(directData);
                                    totalLength = directData.length;
                                }
                            }

                            if (totalLength === 0) {
                                throw new Error('No data received');
                            }

                            const result = new Uint8Array(totalLength);
                            let offset = 0;
                            chunksCopy.forEach(function(chunk) {
                                result.set(chunk, offset);
                                offset += chunk.length;
                            });

                            let filename = parseFilenameFromDisposition(
                                xhr.getResponseHeader("Content-Disposition")
                            );
                            if (!filename) {
                                filename = generateSafeFilename(item.id, index, total);
                            }

                            const blob = new Blob([result.buffer], { type: "application/octet-stream" });
                            const link = document.createElement("a");
                            link.href = URL.createObjectURL(blob);
                            link.download = filename;
                            document.body.appendChild(link);
                            link.click();
                            document.body.removeChild(link);
                            URL.revokeObjectURL(link.href);

                            updateProgressItem(index, 'done', '', totalLength, 100);
                            resolve({ success: true, filename: filename, size: totalLength });
                        } catch (e) {
                            updateProgressItem(index, 'error', 'Save failed: ' + e.message, 0, 0);
                            reject(new Error('Save failed: ' + e.message));
                        }
                    } else {
                        let errorMsg = 'HTTP ' + xhr.status;
                        try {
                            const resp = JSON.parse(xhr.responseText);
                            if (resp.info && resp.info.message) {
                                errorMsg = resp.info.message;
                            }
                        } catch (e) {
                            // ignore
                        }
                        updateProgressItem(index, 'error', 'Fail: ' + errorMsg, 0, 0);
                        reject(new Error(errorMsg));
                    }
                }
            };

            try {
                xhr.send(formData);
            } catch (e) {
                updateProgressItem(index, 'error', 'Send failed: ' + e.message, 0, 0);
                reject(e);
            }
        });
    }

    /**
     * 执行 range 模式备份
     */
    async function executeRangeBackup() {
        const els = getElements();
        const startInput = els.start;
        const sizeInput = els.size;

        // 验证输入
        const validation = validateRangeInput();
        if (!validation.valid) {
            const errorMessages = validation.errors.map(function(e) {
                return e.message;
            }).join('\n');
            alert(t('backup.error.invalid_input') + ':\n' + errorMessages);
            return;
        }

        const selectedItem = getSelectedRangeItem();
        if (!selectedItem) {
            alert(t('backup.error.range_no_selection'));
            return;
        }

        totalDownloadSize = 0;
        downloadedSize = 0;
        progressItemsMap.clear();

        const formData = new FormData();
        formData.append("mode", "range");
        formData.append("target", selectedItem.id);
        formData.append("start", String(validation.start));
        formData.append("size", String(validation.size));

        isDownloading = true;

        showProgressArea();
        addProgressItem(0, 1, selectedItem.display, validation.size);
        updateOverallStatus(t('backup.status.downloading'));

        try {
            const response = await fetch("/backup", { method: "POST", body: formData });

            if (!response.ok) {
                updateProgressItem(0, 'error', 'HTTP ' + response.status, 0, 0);
                updateOverallStatus(t('backup.status.error'));
                return;
            }

            const contentLength = response.headers.get("Content-Length");
            const totalSize = contentLength ? parseInt(contentLength, 10) : 0;
            let filename = parseFilenameFromDisposition(
                response.headers.get("Content-Disposition")
            );
            if (!filename) filename = "backup.bin";

            let received = 0;
            const chunks = [];
            const reader = response.body.getReader();

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;
                chunks.push(value);
                received += value.length;
                downloadedSize = received;
                if (totalSize) {
                    const percent = (received / totalSize) * 100;
                    updateProgressItem(0, 'downloading', '', received, percent);
                }
                updateOverallStatus(
                    t("backup.status.downloading") + " " +
                    bytesToHuman(received) +
                    (totalSize ? " / " + bytesToHuman(totalSize) : "")
                );
                updateOverallProgress();
            }

            const blob = new Blob(chunks, { type: "application/octet-stream" });
            const link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);

            updateProgressItem(0, 'done', '', blob.size, 100);
            updateOverallStatus(t('backup.status.done') + ': ' + filename);

        } catch (error) {
            updateProgressItem(0, 'error', error.message || 'Unknown error', 0, 0);
            updateOverallStatus(t('backup.status.error'));
        }

        isDownloading = false;
    }

    /**
     * 执行 full 模式备份（批量下载）
     */
    async function executeFullBackup() {
        const els = getElements();
        const useChinese = APP_STATE.lang && APP_STATE.lang === 'zh-cn';

        const selectedIndices = Array.from(selectedItems).sort((a, b) => a - b);
        if (selectedIndices.length === 0) {
            alert(t('backup.error.no_selection'));
            return;
        }

        const confirmMsg = t('backup.confirm.download') + `${selectedIndices.length}`;
        if (!confirm(confirmMsg)) {
            return;
        }

        totalDownloadSize = 0;
        downloadedSize = 0;
        progressItemsMap.clear();

        isDownloading = true;
        showProgressArea();
        updateOverallStatus(useChinese
            ? `开始下载 ${selectedIndices.length} 个条目...`
            : `Starting downloading ${selectedIndices.length} items...`
        );

        selectedIndices.forEach(function(idx, order) {
            const item = itemsData[idx];
            const itemSize = getItemSize(item);
            addProgressItem(order, selectedIndices.length, item.display, itemSize);
        });

        let successCount = 0;
        let failCount = 0;
        const errors = [];

        for (let order = 0; order < selectedIndices.length; order++) {
            const idx = selectedIndices[order];
            const item = itemsData[idx];

            updateOverallStatus((useChinese ? '正在下载' : 'Downloading') +
                ` (${order + 1}/${selectedIndices.length}): ${item.display}`
            );

            try {
                await downloadItem(item, order, selectedIndices.length);
                successCount++;
            } catch (error) {
                failCount++;
                errors.push({ name: item.display, error: error.message });
                updateProgressItem(order, 'error', 'Fail: ' + error.message, 0, 0);
            }
        }

        let finalStatus = useChinese
            ? `下载完成：✅ ${successCount} 成功`
            : `Download complete: ✅ ${successCount} successfully`;
        if (failCount > 0) {
            finalStatus += useChinese ? `，❌ ${failCount} 失败` : `, ❌ ${failCount} failed`;
            if (errors.length > 0) {
                finalStatus += useChinese ? '（详情见下方列表）' : ' (see the list below for details)';
            }
        }
        updateOverallStatus(finalStatus);

        isDownloading = false;

        if (errors.length > 0) {
            let errorMsg = useChinese ? '以下条目下载失败：\n' : 'The following items failed to download:\n';
            errors.forEach(function(e) {
                errorMsg += `- ${e.name}: ${e.error}\n`;
            });
            alert(errorMsg);
        }
    }

    /**
     * 开始备份
     */
    async function startBackup() {
        const els = getElements();
        const mode = els.mode?.value;

        if (isDownloading) {
            alert(t('backup.error.downloading'));
            return;
        }

        if (!els.target.value) {
            alert(t('backup.error.no_target'));
            return;
        }

        if (mode === 'range') {
            await executeRangeBackup();
        } else {
            await executeFullBackup();
        }
    }

    /**
     * 初始化备份管理器
     */
    async function init() {
        const els = getElements();

        if (els.mode) {
            els.mode.addEventListener("change", onModeChange);
        }

        if (els.start) {
            els.start.addEventListener("input", function() {
                validateRangeInput();
            });
        }
        if (els.size) {
            els.size.addEventListener("input", function() {
                validateRangeInput();
            });
        }

        loadTargets();

        hideProgressArea();
        itemsData = [];
        rangeItemsData = [];
        selectedItems.clear();
        rangeSelectedValue = "";
        renderItems();
        renderRangeSelect();
        updateModeUI();
        resetValidation();
    }

    return {
        init,
        startBackup,
        loadTargets,
        selectAll,
        selectAllForce,
        deselectAll,
        getSelectedCount: () => selectedItems.size,
        getTotalCount: () => itemsData.length,
    };
})();

// ==============================
// 环境变量管理模块
// ==============================

/**
 * 环境变量管理器
 * 负责处理 U-Boot 环境变量的查看、添加、修改、删除等操作
 */
const envManager = (() => {
    let elements = null;
    let readOnly;
    let bPersistentMode = false;
    let envData = []; // 存储解析后的环境变量数据
    let refreshTimer = null; // 延迟刷新定时器
    let isAddingNew = false; // 是否正在添加新变量

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            tableBody: document.getElementById("env_table_body"),
            tableContainer: document.getElementById("env_table_container"),
            envTable: document.getElementById("env_table"),
            emptyHint: document.getElementById("env_empty"),
            status: document.getElementById("env_status"),
            count: document.getElementById("env_count"),
            restoreFile: document.getElementById("env_restore_file"),
            hint: document.getElementById("env_hint"),
            readonlyHint: document.getElementById("env_readonly_hint"),
            protectedHint: document.getElementById("env_protected_hint"),
        };

        return elements;
    }

    /**
     * 设置状态提示
     */
    function setStatus(text, isError) {
        const el = getElements().status;
        if (el) {
            el.textContent = text || "";
            el.style.color = isError ? "var(--danger)" : "";
        }
    }

    /**
     * 格式化错误信息
     */
    function formatError(error) {
        if (!error) return t("error.unknown");
        if (error.message) return error.message;
        return String(error);
    }

    /**
     * 取消之前的延迟刷新定时器
     */
    function cancelDelayedRefresh() {
        if (refreshTimer) {
            clearTimeout(refreshTimer);
            refreshTimer = null;
        }
    }

    /**
     * 延迟刷新环境变量列表
     */
    function delayedRefresh(delay) {
        cancelDelayedRefresh();
        delay = delay || 1500;
        refreshTimer = setTimeout(async () => {
            refreshTimer = null;
            await refresh();
        }, delay);
    }

    /**
     * 更新空状态显示
     * 检查表格中是否还有行，如果没有则显示空状态提示
     */
    function updateEmptyState() {
        const rows = document.querySelectorAll('.env-row:not([data-is-new="true"])');
        const els = getElements();

        if (els.emptyHint) {
            els.emptyHint.style.display = (rows.length === 0) ? 'flex' : 'none';
        }

        if (els.envTable) {
            els.envTable.style.display = (rows.length === 0) ? 'none' : 'table';
        }
    }

    /**
     * 取消所有行的编辑状态
     */
    function cancelAllEditing() {
        document.querySelectorAll('.env-row.editing').forEach((row) => {
            // 如果是新增行，直接移除
            if (row.dataset.isNew === 'true') {
                row.remove();
                isAddingNew = false;
                return;
            }

            const inp = row.querySelector('.env-value-input');
            const roGroup = row.querySelector('.env-action-group-readonly');
            const edGroup = row.querySelector('.env-action-group-editing');
            if (inp) {
                inp.value = inp.dataset.originalValue || '';
                inp.readOnly = true;
                inp.disabled = true;
                inp.classList.remove('editing');
            }
            if (roGroup) roGroup.style.display = '';
            if (edGroup) edGroup.style.display = 'none';
            row.classList.remove('editing');
        });

        updateEmptyState();
        isAddingNew = false;
    }

    /**
     * 创建操作列按钮组
     */
    function createActionGroups(row, inputEl, isNewRow = false) {
        const tdActions = document.createElement('td');
        tdActions.className = 'env-col-actions';

        // ===== 只读状态按钮组 =====
        const readOnlyGroup = document.createElement('span');
        readOnlyGroup.className = 'env-action-group env-action-group-readonly';
        if (isNewRow) {
            readOnlyGroup.style.display = 'none';
        }

        // 编辑按钮
        const protectedRow = !isNewRow && isRt3000BProtectedEnvName(row.dataset.key, bPersistentMode);
        const editBtn = document.createElement('button');
        editBtn.type = 'button';
        editBtn.disabled = protectedRow;
        editBtn.className = 'button env-action-btn';
        editBtn.textContent = t('env.action.edit') || 'Edit';
        editBtn.dataset.action = 'edit';
        editBtn.addEventListener('click', () => {
            enterEditMode(row, inputEl, readOnlyGroup, editGroup);
        });

        // 删除按钮
        const deleteBtn = document.createElement('button');
        deleteBtn.type = 'button';
        deleteBtn.disabled = protectedRow;
        deleteBtn.className = 'button button-danger env-action-btn';
        deleteBtn.textContent = t('env.action.delete') || 'Delete';
        deleteBtn.dataset.action = 'delete';
        deleteBtn.addEventListener('click', () => {
            if (isNewRow) {
                // 新增行直接移除
                row.remove();
                isAddingNew = false;
                updateEmptyState();
                setStatus(t('env.status.ready'));
                return;
            }
            deleteSingle(row.dataset.key);
        });

        readOnlyGroup.appendChild(editBtn);
        readOnlyGroup.appendChild(deleteBtn);

        // ===== 编辑状态按钮组 =====
        const editGroup = document.createElement('span');
        editGroup.className = 'env-action-group env-action-group-editing';
        if (!isNewRow) {
            editGroup.style.display = 'none';
        }

        // 保存按钮
        const saveBtn = document.createElement('button');
        saveBtn.type = 'button';
        saveBtn.className = 'button env-action-btn';
        saveBtn.textContent = t('env.action.save') || 'Save';
        saveBtn.dataset.action = 'save';
        saveBtn.addEventListener('click', () => {
            if (isNewRow) {
                saveNewVariable(row, inputEl, readOnlyGroup, editGroup);
            } else {
                saveInlineEdit(row, inputEl, readOnlyGroup, editGroup);
            }
        });

        // 重置为默认值按钮（仅对已存在的变量显示）
        const resetBtn = document.createElement('button');
        resetBtn.type = 'button';
        resetBtn.disabled = protectedRow;
        resetBtn.className = 'button button-warn env-action-btn';
        resetBtn.textContent = t('env.action.reset_single') || 'Reset';
        resetBtn.dataset.action = 'reset';
        resetBtn.addEventListener('click', () => {
            resetSingleInline(row, inputEl, readOnlyGroup, editGroup);
        });
        if (isNewRow) {
            resetBtn.style.display = 'none';
        }

        // 取消按钮
        const cancelBtn = document.createElement('button');
        cancelBtn.type = 'button';
        cancelBtn.className = 'button env-action-btn';
        cancelBtn.textContent = t('env.action.cancel') || 'Cancel';
        cancelBtn.dataset.action = 'cancel';
        cancelBtn.addEventListener('click', () => {
            if (isNewRow) {
                // 新增行直接移除
                row.remove();
                isAddingNew = false;
                updateEmptyState();
                setStatus(t('env.status.ready'));
                return;
            }
            cancelInlineEdit(row, inputEl, readOnlyGroup, editGroup);
        });

        editGroup.appendChild(saveBtn);
        if (!isNewRow) {
            editGroup.appendChild(resetBtn);
        }
        editGroup.appendChild(cancelBtn);

        tdActions.appendChild(readOnlyGroup);
        tdActions.appendChild(editGroup);

        return tdActions;
    }

    /**
     * 渲染环境变量表格
     */
    function renderTable(data) {
        const els = getElements();

        if (!els.tableBody || !els.envTable || !els.emptyHint) return;

        els.tableBody.innerHTML = '';

        if (!data || data.length === 0) {
            els.emptyHint.style.display = 'flex';
            els.envTable.style.display = 'none';
            return;
        }

        els.emptyHint.style.display = 'none';
        els.envTable.style.display = 'table';

        // 渲染每一行
        data.forEach((item) => {
            const row = createDataRow(item.key, item.value, false);
            els.tableBody.appendChild(row);
        });
    }

    /**
     * 创建数据行
     */
    function createDataRow(key, value, isNewRow = false) {
        const row = document.createElement('tr');
        row.className = 'env-row';
        row.dataset.key = key;
        if (isNewRow) {
            row.dataset.isNew = 'true';
        }

        // 名称列
        const tdName = document.createElement('td');
        tdName.className = 'env-col-name';

        if (isNewRow) {
            // 新增行：名称使用输入框
            const nameInput = document.createElement('input');
            nameInput.type = 'text';
            nameInput.className = 'env-name-input';
            nameInput.placeholder = t('env.placeholder.name') || 'Enter variable name';
            nameInput.autocomplete = 'off';
            nameInput.autocapitalize = 'off';
            nameInput.autocorrect = 'off';
            nameInput.spellcheck = false;
            nameInput.dataset.isNameInput = 'true';
            tdName.appendChild(nameInput);
            // 聚焦到名称输入框
            setTimeout(() => nameInput.focus(), 50);
        } else {
            tdName.textContent = key;
            tdName.title = key;
        }

        // 值列 - 使用输入框
        const tdValue = document.createElement('td');
        tdValue.className = 'env-col-value';

        const valueInput = document.createElement('input');
        valueInput.type = 'text';
        valueInput.className = 'env-value-input';
        valueInput.value = value || '';
        valueInput.readOnly = !isNewRow;
        valueInput.disabled = !isNewRow;
        valueInput.title = value || '';
        valueInput.dataset.originalValue = value || '';
        valueInput.dataset.key = key;
        if (isNewRow) {
            valueInput.classList.add('editing');
            valueInput.placeholder = t('env.placeholder.value') || 'Enter variable value';
        }
        tdValue.appendChild(valueInput);

        // 操作列
        row.appendChild(tdName);
        row.appendChild(tdValue);
        if (!readOnly) {
            const tdActions = createActionGroups(row, valueInput, isNewRow);
            row.appendChild(tdActions);
        }

        // 如果是新行，自动进入编辑状态
        if (isNewRow) {
            row.classList.add('editing');
            // 名称输入框获得焦点
            const nameInput = tdName.querySelector('.env-name-input');
            if (nameInput) {
                setTimeout(() => nameInput.focus(), 100);
            }
        }

        return row;
    }

    /**
     * 进入行内编辑模式
     */
    function enterEditMode(row, inputEl, readOnlyGroup, editGroup) {
        if (isRt3000BProtectedEnvName(row.dataset.key, bPersistentMode)) return;
        // 先取消其他行的编辑状态（包括新增行）
        document.querySelectorAll('.env-row.editing').forEach((r) => {
            if (r !== row) {
                if (r.dataset.isNew === 'true') {
                    // 移除其他新增行
                    r.remove();
                    isAddingNew = false;
                    return;
                }
                const inp = r.querySelector('.env-value-input');
                const roGroup = r.querySelector('.env-action-group-readonly');
                const edGroup = r.querySelector('.env-action-group-editing');
                if (inp) {
                    inp.value = inp.dataset.originalValue || '';
                    inp.readOnly = true;
                    inp.disabled = true;
                    inp.classList.remove('editing');
                }
                if (roGroup) roGroup.style.display = '';
                if (edGroup) edGroup.style.display = 'none';
                r.classList.remove('editing');
            }
        });

        // 进入编辑模式
        row.classList.add('editing');
        inputEl.readOnly = false;
        inputEl.disabled = false;
        inputEl.classList.add('editing');
        inputEl.focus();
        inputEl.select();

        readOnlyGroup.style.display = 'none';
        editGroup.style.display = '';

        setStatus(t('env.status.edit_mode') + row.dataset.key);
    }

    /**
     * 保存新变量
     */
    async function saveNewVariable(row, inputEl, readOnlyGroup, editGroup) {
        const nameInput = row.querySelector('.env-name-input');
        const name = nameInput ? nameInput.value.trim() : '';
        const value = inputEl.value;

        if (!name) {
            alert(t("env.error.no_name"));
            nameInput?.focus();
            return;
        }

        // 检查名称是否已存在
        const existingRow = document.querySelector(`.env-row[data-key="${name}"]`);
        if (existingRow && existingRow !== row) {
            alert(`${name}: ${t("env.error.already_exists") || 'Variable already exists!'}`);
            nameInput?.focus();
            return;
        }

        try {
            setStatus(t('env.status.saving'));

            const formData = new FormData();
            formData.append("name", name);
            formData.append("value", value);

            const response = await fetch("/env/set", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            // 更新行数据
            row.dataset.key = name;
            delete row.dataset.isNew;
            row.classList.remove('editing');

            // 更新名称列为纯文本
            const tdName = row.querySelector('.env-col-name');
            tdName.innerHTML = '';
            tdName.textContent = name;
            tdName.title = name;

            // 更新值输入框
            inputEl.dataset.originalValue = value;
            inputEl.readOnly = true;
            inputEl.disabled = true;
            inputEl.classList.remove('editing');
            inputEl.removeAttribute('placeholder');

            // 切换按钮组
            readOnlyGroup.style.display = '';
            editGroup.style.display = 'none';

            // 更新重置按钮显示
            const resetBtn = editGroup.querySelector('[data-action="reset"]');
            if (resetBtn) {
                resetBtn.style.display = '';
            }

            isAddingNew = false;
            setStatus(t('env.status.saved'));
            delayedRefresh();

        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 保存行内编辑
     */
    async function saveInlineEdit(row, inputEl, readOnlyGroup, editGroup) {
        const key = row.dataset.key;
        if (isRt3000BProtectedEnvName(key, bPersistentMode)) return;
        const newValue = inputEl.value;

        // 如果值没有变化，直接取消编辑
        if (newValue === inputEl.dataset.originalValue) {
            cancelInlineEdit(row, inputEl, readOnlyGroup, editGroup);
            return;
        }

        try {
            setStatus(t('env.status.saving'));

            const formData = new FormData();
            formData.append("name", key);
            formData.append("value", newValue);

            const response = await fetch("/env/set", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            inputEl.dataset.originalValue = newValue;
            inputEl.value = newValue;

            cancelAllEditing();
            setStatus(t('env.status.saved'));
            delayedRefresh();

        } catch (error) {
            cancelAllEditing();
            setStatus(formatError(error), true);
        }
    }

    /**
     * 重置单行变量为默认值（行内）
     */
    async function resetSingleInline(row, inputEl, readOnlyGroup, editGroup) {
        const key = row.dataset.key;
        if (isRt3000BProtectedEnvName(key, bPersistentMode)) return;

        if (!confirm(`${key}: ${t("env.confirm.reset_single")}`)) return;

        try {
            setStatus(t('env.status.saving'));

            const formData = new FormData();
            formData.append("name", key);

            const response = await fetch("/env/reset/single", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            cancelAllEditing();
            setStatus(t('env.status.reset_single'));
            delayedRefresh();

        } catch (error) {
            cancelAllEditing();
            setStatus(formatError(error), true);
        }
    }

    /**
     * 取消行内编辑
     */
    function cancelInlineEdit(row, inputEl, readOnlyGroup, editGroup) {
        // 恢复原始值
        inputEl.value = inputEl.dataset.originalValue || '';
        inputEl.readOnly = true;
        inputEl.disabled = true;
        inputEl.classList.remove('editing');

        row.classList.remove('editing');

        readOnlyGroup.style.display = '';
        editGroup.style.display = 'none';

        setStatus(t('env.status.ready'));
    }

    /**
     * 刷新环境变量列表（立即执行）
     */
    async function refresh() {
        cancelDelayedRefresh();

        const els = getElements();

        // 如果有新增行，先取消
        if (isAddingNew) {
            const newRow = document.querySelector('.env-row[data-is-new="true"]');
            if (newRow) {
                newRow.remove();
                isAddingNew = false;
            }
        }

        try {
            setStatus(t("env.status.loading"));

            const response = await fetch("/env/list", {
                method: "GET",
                cache: "no-store",
                headers: {
                    "Accept": "text/plain"
                }
            });

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}`);
            }

            const text = await response.text();

            // 解析纯文本：每行 key=value
            envData = [];
            const lines = text.split('\n');

            for (const line of lines) {
                const trimmed = line.trim();
                if (!trimmed) continue;

                const equalPos = trimmed.indexOf('=');
                if (equalPos > 0) {
                    const key = trimmed.substring(0, equalPos).trim();
                    const value = trimmed.substring(equalPos + 1);
                    if (key) {
                        envData.push({ key, value });
                    }
                } else if (equalPos === -1 && trimmed) {
                    envData.push({ key: trimmed, value: '' });
                }
            }

            envData.sort((a, b) => a.key.localeCompare(b.key));
            renderTable(envData);

            if (els.count) {
                els.count.textContent = `${t("env.count")} ${envData.length}`;
            }

            setStatus(t("env.status.ready"));

        } catch (error) {
            console.error("[envManager] Error:", error);
            setStatus(formatError(error), true);
        }
    }

    /**
     * 添加新变量（在表格中创建新行）
     */
    function addVariable() {
        if (readOnly) return;
        // 如果有正在编辑的行，先取消
        cancelAllEditing();

        const els = getElements();

        // 如果表格为空，显示表格
        if (els.emptyHint) els.emptyHint.style.display = 'none';
        if (els.envTable) els.envTable.style.display = 'table';

        // 创建新行
        const newRow = createDataRow('', '', true);
        els.tableBody.appendChild(newRow);
        isAddingNew = true;

        setStatus(t('env.status.adding') || 'Adding new variable...');
    }

    /**
     * 删除单个环境变量
     */
    async function deleteSingle(name) {
        if (readOnly || isRt3000BProtectedEnvName(name, bPersistentMode)) return;
        const excludeList = ['baudrate', 'stderr', 'stdin', 'stdout'];
        if (excludeList.includes(name)) {
            alert(name + ": " + t("env.delete.forbid"));
            return;
        }

        if (!confirm(`${t("env.confirm.delete")} "${name}" ?`)) return;

        try {
            setStatus(t("env.status.saving"));

            const formData = new FormData();
            formData.append("name", name);

            const response = await fetch("/env/unset", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            setStatus(t("env.status.deleted"));
            delayedRefresh();
        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 重置所有环境变量为默认值
     */
    async function resetAll() {
        if (readOnly) return;
        if (!confirm(t("env.confirm.reset"))) return;

        try {
            setStatus(t("env.status.saving"));
            const response = await fetch("/env/reset/all", { method: "POST" });
            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            setStatus(t("env.status.reset"));
            delayedRefresh();
        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 从文件恢复环境变量
     * 点击恢复按钮后弹出文件选择框
     */
    function restore() {
        if (readOnly) return;
        const els = getElements();

        if (!els.restoreFile) return;

        // 清除之前的选择，确保 change 事件能触发
        els.restoreFile.value = '';

        // 触发文件选择
        els.restoreFile.click();
    }

    /**
     * 处理恢复文件选择
     * @param {File} file - 用户选择的文件
     */
    async function handleRestoreFile(file) {
        if (readOnly) return;
        if (!file) return;

        if (!confirm(t("env.confirm.restore"))) return;

        try {
            setStatus(t("env.status.saving"));

            const formData = new FormData();
            formData.append("envfile", file);

            const response = await fetch("/env/restore", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("env.status.http")} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t("env.status.error"));
            }

            setStatus(t("env.status.restored"));
            delayedRefresh();
        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 初始化环境变量管理器
     */
    async function init() {
        const els = getElements();

        // 绑定恢复文件选择事件
        if (els.restoreFile) {
            els.restoreFile.addEventListener('change', function() {
                const file = this.files[0];
                if (file) handleRestoreFile(file);
            });
        }

        // 点击页面其他区域时，取消正在编辑的行
        document.addEventListener('click', function(e) {
            const row = e.target.closest('.env-row');
            const isButton = e.target.closest('button');
            const isInput = e.target.closest('input');

            // 如果点击的是按钮或输入框，不处理
            if (isButton || isInput) return;

            if (!row) {
                cancelAllEditing();
                setStatus(t('env.status.ready'));
            }
        });

        const mutationButtons = document.querySelectorAll(".env-main-actions button");
        mutationButtons.forEach((button) => {
            if (button.dataset.i18n !== "env.action.refresh") button.disabled = true;
        });
        try {
            const response = await fetch("/env/info", { cache: "no-store" });
            if (response.ok) {
                const info = await response.json();
                if (Object.prototype.hasOwnProperty.call(info, "persistent_write_supported")) {
                    bPersistentMode = info.read_only === false &&
                        info.persistent_write_supported === true &&
                        info.persistent_target === "0:APPSBLENV";
                    readOnly = !bPersistentMode;
                } else {
                    bPersistentMode = false;
                    readOnly = info.read_only === true;
                }
            }
        } catch (error) {
            // Older board builds do not expose environment capability metadata.
        }

        if (els.protectedHint) els.protectedHint.hidden = !bPersistentMode;
        if (readOnly) {
            if (els.hint) els.hint.hidden = true;
            if (els.readonlyHint) els.readonlyHint.hidden = false;
            const mutationWarnings = document.getElementById("env_mutation_warnings");
            if (mutationWarnings) mutationWarnings.hidden = true;
            document.querySelectorAll(".env-main-actions button").forEach((button) => {
                if (button.dataset.i18n !== "env.action.refresh") button.disabled = true;
            });
            if (els.restoreFile) els.restoreFile.disabled = true;
            const actionHeader = document.querySelector("#env_table thead .env-col-actions");
            if (actionHeader) actionHeader.style.display = "none";
        } else {
            mutationButtons.forEach((button) => { button.disabled = false; });
        }

        await refresh();
    }

    return {
        init,
        refresh,
        addVariable,
        deleteSingle,
        resetAll,
        restore,
    };
})();

// ==============================
// 网络参数管理模块
// ==============================

/**
 * 网络参数管理器
 * 负责处理 U-Boot 网络参数（ipaddr、netmask、serverip）的查看、设置、重置操作
 */
const networkManager = (() => {
    let elements = null;
    let validationState = {
        ipaddr: true,
        netmask: true,
        serverip: true
    };
    let refreshTimer = null; // 延迟刷新定时器

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            status: document.getElementById("network_status"),
            ipaddr: document.getElementById("network_ipaddr"),
            netmask: document.getElementById("network_netmask"),
            serverip: document.getElementById("network_serverip"),
            hintIpaddr: document.getElementById("hint_ipaddr"),
            hintNetmask: document.getElementById("hint_netmask"),
            hintServerip: document.getElementById("hint_serverip")
        };

        return elements;
    }

    /**
     * 设置状态提示
     */
    function setStatus(text, isError) {
        const el = getElements().status;
        if (el) {
            el.textContent = text || "";
            if (isError) {
                el.style.color = "var(--danger)";
            } else {
                el.style.color = "";
            }
        }
    }

    /**
     * 验证 IP 地址格式
     * @param {string} ip - 待验证的 IP 地址
     * @returns {boolean} 是否为有效 IP 地址
     */
    function isValidIP(ip) {
        if (!ip || typeof ip !== 'string') return false;

        // IP 地址正则表达式（IPv4）
        const ipRegex = /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
        return ipRegex.test(ip.trim());
    }

    /**
     * 验证子网掩码格式
     * @param {string} mask - 待验证的子网掩码
     * @returns {boolean} 是否为有效子网掩码
     */
    function isValidNetmask(mask) {
        if (!mask || typeof mask !== 'string') return false;

        // 先检查是否为有效 IP 格式
        if (!isValidIP(mask)) return false;

        // 检查是否为有效的子网掩码（连续的1后跟连续的0）
        const parts = mask.trim().split('.').map(Number);
        const binary = parts.reduce((acc, part) => acc + part.toString(2).padStart(8, '0'), '');

        // 检查是否为连续的1后跟连续的0
        const firstZero = binary.indexOf('0');
        if (firstZero === -1) return true; // 全1 (255.255.255.255)
        const lastOne = binary.lastIndexOf('1');
        return lastOne < firstZero;
    }

    /**
     * 更新单个字段的验证提示
     * @param {string} field - 字段名
     * @param {string} value - 字段值
     */
    function updateFieldValidation(field, value) {
        const els = getElements();
        const inputEl = els[field];
        const hintEl = els[`hint${field.charAt(0).toUpperCase() + field.slice(1)}`];

        if (!hintEl || !inputEl) return;

        // 清除之前的样式
        inputEl.classList.remove('input-valid', 'input-invalid');

        if (!value || value.trim() === '') {
            hintEl.textContent = '';
            hintEl.className = 'field-hint';
            validationState[field] = true;
            return;
        }

        let isValid = false;
        if (field === 'netmask') {
            isValid = isValidNetmask(value);
        } else {
            isValid = isValidIP(value);
        }

        validationState[field] = isValid;

        if (isValid) {
            hintEl.textContent = '✓ ' + t('network.validation.valid');
            hintEl.className = 'field-hint field-hint-valid';
            inputEl.classList.add('input-valid');
        } else {
            if (field === 'netmask') {
                hintEl.textContent = '✗ ' + t('network.validation.invalid_netmask');
            } else {
                hintEl.textContent = '✗ ' + t('network.validation.invalid_ip');
            }
            hintEl.className = 'field-hint field-hint-invalid';
            inputEl.classList.add('input-invalid');
        }
    }

    /**
     * 表单验证（保存前检查）
     * @returns {boolean} 是否通过验证
     */
    function validateForm() {
        const els = getElements();
        const ipaddr = els.ipaddr?.value?.trim() || '';
        const netmask = els.netmask?.value?.trim() || '';
        const serverip = els.serverip?.value?.trim() || '';

        // 重新验证所有字段
        updateFieldValidation('ipaddr', ipaddr);
        updateFieldValidation('netmask', netmask);
        updateFieldValidation('serverip', serverip);

        const errors = [];

        // 获取字段标签文本
        const labelIpaddr = t('network.label.ipaddr').replace(':', '');
        const labelNetmask = t('network.label.netmask').replace(':', '');
        const labelServerip = t('network.label.serverip').replace(':', '');

        if (!ipaddr) {
            errors.push(labelIpaddr + ' ' + t('network.validation.cannot_be_empty'));
        } else if (!validationState.ipaddr) {
            errors.push(labelIpaddr + ' ' + t('network.validation.invalid_format'));
        }

        if (!netmask) {
            errors.push(labelNetmask + ' ' + t('network.validation.cannot_be_empty'));
        } else if (!validationState.netmask) {
            errors.push(labelNetmask + ' ' + t('network.validation.invalid_netmask_format'));
        }

        if (!serverip) {
            errors.push(labelServerip + ' ' + t('network.validation.cannot_be_empty'));
        } else if (!validationState.serverip) {
            errors.push(labelServerip + ' ' + t('network.validation.invalid_format'));
        }

        if (errors.length > 0) {
            setStatus(errors.join('; '), true);
            return false;
        }

        return true;
    }

    /**
     * 刷新网络参数信息
     */
    async function refresh() {
        cancelDelayedRefresh();

        try {
            setStatus(t('network.status.loading'));

            const response = await fetch("/network/info", {
                method: "GET",
                cache: "no-store",
                headers: {
                    "Accept": "application/json"
                }
            });

            if (!response.ok) {
                throw new Error(`${t('network.status.http')} ${response.status}`);
            }

            const data = await response.json();

            // 更新输入框
            const els = getElements();
            if (els.ipaddr) {
                els.ipaddr.value = data.ipaddr || '';
                updateFieldValidation('ipaddr', els.ipaddr.value);
            }
            if (els.netmask) {
                els.netmask.value = data.netmask || '';
                updateFieldValidation('netmask', els.netmask.value);
            }
            if (els.serverip) {
                els.serverip.value = data.serverip || '';
                updateFieldValidation('serverip', els.serverip.value);
            }

            setStatus(t('network.status.ready'));

        } catch (error) {
            setStatus(formatError(error), true);
            console.error("Network Manager Error:", error);
        }
    }

    /**
     * 格式化错误信息
     */
    function formatError(error) {
        if (!error) return t("error.unknown");
        if (error.message) return error.message;
        return String(error);
    }

    /**
     * 取消之前的延迟刷新定时器
     */
    function cancelDelayedRefresh() {
        if (refreshTimer) {
            clearTimeout(refreshTimer);
            refreshTimer = null;
        }
    }

    /**
     * 延迟刷新环境变量列表
     * 先显示成功状态，延迟一段时间后再刷新
     * @param {number} delay - 延迟时间（毫秒），默认 1500ms
     */
    function delayedRefresh(delay) {
        cancelDelayedRefresh();

        delay = delay || 1500; // 默认 1.5 秒

        refreshTimer = setTimeout(async () => {
            refreshTimer = null;
            await refresh();
        }, delay);
    }

    /**
     * 保存网络参数
     */
    async function save() {
        // 先验证表单
        if (!validateForm()) {
            return;
        }

        const els = getElements();
        const ipaddr = els.ipaddr.value.trim();
        const netmask = els.netmask.value.trim();
        const serverip = els.serverip.value.trim();

        try {
            setStatus(t('network.status.saving'));

            const formData = new FormData();
            formData.append("ipaddr", ipaddr);
            formData.append("netmask", netmask);
            formData.append("serverip", serverip);

            const response = await fetch("/network/set", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t('network.status.http')} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t('network.status.error'));
            }

            setStatus(t('network.status.saved'));

            // 刷新显示
            delayedRefresh();
        } catch (error) {
            setStatus(formatError(error), true);
            console.error("Network Manager Error:", error);
        }
    }

    /**
     * 重置为默认网络参数
     */
    async function reset() {
        if (!confirm(t('network.confirm.reset'))) {
            return;
        }

        try {
            setStatus(t('network.status.saving'));

            const response = await fetch("/network/reset", {
                method: "POST"
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t('network.status.http')} ${response.status}: ${result}`);
            }

            if (result !== "ok") {
                throw new Error(result || t('network.status.error'));
            }

            setStatus(t('network.status.reset'));

            delayedRefresh();

        } catch (error) {
            setStatus(formatError(error), true);
            console.error("Network Manager Error:", error);
        }
    }

    /**
     * 绑定输入验证事件
     */
    function bindValidationEvents() {
        const els = getElements();

        // 为每个输入框绑定实时验证
        const fields = ['ipaddr', 'netmask', 'serverip'];

        fields.forEach(field => {
            const inputEl = els[field];
            if (!inputEl) return;

            // 输入事件（实时验证）
            inputEl.addEventListener('input', () => {
                updateFieldValidation(field, inputEl.value);
            });

            // 失焦事件（清理空格）
            inputEl.addEventListener('blur', () => {
                inputEl.value = inputEl.value.trim();
                updateFieldValidation(field, inputEl.value);
            });

            // 粘贴事件（立即验证）
            inputEl.addEventListener('paste', () => {
                setTimeout(() => {
                    updateFieldValidation(field, inputEl.value);
                }, 0);
            });
        });
    }

    /**
     * 初始化网络管理器
     */
    async function init() {
        getElements();

        bindValidationEvents();

        try {
            const response = await fetch("/env/info", { cache: "no-store" });
            if (response.ok) {
                const info = await response.json();
                readOnly = info.read_only === true;
            }
        } catch (error) {
            // Older board builds do not expose environment capability metadata.
        }

        if (readOnly) {
            if (els.hint) els.hint.hidden = true;
            if (els.readonlyHint) els.readonlyHint.hidden = false;
            const mutationWarnings = document.getElementById("env_mutation_warnings");
            if (mutationWarnings) mutationWarnings.hidden = true;
            document.querySelectorAll(".env-main-actions button").forEach((button) => {
                if (button.dataset.i18n !== "env.action.refresh") button.disabled = true;
            });
            if (els.restoreFile) els.restoreFile.disabled = true;
            const actionHeader = document.querySelector("#env_table thead .env-col-actions");
            if (actionHeader) actionHeader.style.display = "none";
        }

        await refresh();
    }

    return {
        init,
        refresh,
        save,
        reset
    };
})();

// ==============================
// 网页终端模块
// ==============================

/**
 * 网页终端管理器
 * 负责处理 U-Boot 命令的发送、输出接收和管理，以及文件上传功能和命令自动补全提示
 */
const webTerminalManager = (() => {
    let elements = null;
    let state = {
        history: [],
        histPos: -1,
        persistKey: "failsafe_webterm_output",
        persistMax: 200000,
        commands: [],           // 存储所有命令对象 {name, usage}
        commandNames: new Map(), // 存储命令名称到完整对象的映射
        debounceTimer: null,
        currentMatch: null,
        isCommandLoaded: false,
        loadingCommands: false,
        fileInfoTimeout: null,
        forbiddenCommands: new Map([
            ['editenv', {
                reasonKey: 'webterm.cmd.forbid.reason.common'
            }],
            ['mm', {
                reasonKey: 'webterm.cmd.forbid.reason.common'
            }],
            ['nm', {
                reasonKey: 'webterm.cmd.forbid.reason.common'
            }],
            ['loop', {
                reasonKey: 'webterm.cmd.forbid.reason.common'
            }],
            ['go', {
                reasonKey: 'webterm.cmd.forbid.reason.common'
            }]
        ])
    };

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            main: document.getElementById("terminal_main"),
            output: document.getElementById("terminal_output"),
            input: document.getElementById("terminal_input"),
            status: document.getElementById("terminal_status"),
            fileInfo: document.getElementById("terminal_file_info"),
            fileMessage: document.getElementById("terminal_file_message"),
            fileProgress: document.getElementById("terminal_file_progress"),
            progressCircle: document.getElementById("bar-circle-mini"),
            suggestionsBox: document.getElementById("cmd_suggestions"),
            suggestionsList: document.getElementById("suggestions_list")
        };

        return elements;
    }

    /**
     * 设置状态栏文本
     */
    function setStatus(text, isError) {
        const el = getElements().status;
        if (el) {
            el.textContent = text || "Ready";
            if (isError) {
                el.classList.add('error');
                el.classList.remove('warning');
            } else {
                el.classList.remove('error', 'warning');
            }
        }
    }

    /**
     * 显示文件信息
     */
    function showFileInfo(text, isSuccess) {
        const els = getElements();

        // 清除之前的自动隐藏定时器
        if (state.fileInfoTimeout) {
            clearTimeout(state.fileInfoTimeout);
            state.fileInfoTimeout = null;
        }

        if (!text) {
            els.fileInfo.classList.remove('show');
            return;
        }

        // 显示文件信息区域
        els.fileInfo.classList.add('show');

        // 设置消息内容和样式
        if (els.fileMessage) {
            els.fileMessage.textContent = text;
            if (isSuccess === true) {
                els.fileMessage.classList.add('success');
                els.fileMessage.classList.remove('error');
            } else if (isSuccess === false) {
                els.fileMessage.classList.add('error');
                els.fileMessage.classList.remove('success');
            } else {
                els.fileMessage.classList.remove('success', 'error');
            }
        }

        // 2秒后自动隐藏
        state.fileInfoTimeout = setTimeout(() => {
            els.fileInfo.classList.remove('show');
            state.fileInfoTimeout = null;
        }, 2000);
    }

    /**
     * 显示/隐藏文件上传进度
     */
    function showFileProgress(show) {
        const el = getElements().fileProgress;
        if (el) {
            el.style.display = show ? "flex" : "none";
        }
    }

    /**
     * 设置文件上传进度
     */
    function setFileProgress(percent) {
        const el = getElements().progressCircle;
        if (el) {
            const p = Math.max(0, Math.min(100, parseInt(percent || 0)));
            el.style.setProperty("--percent", p);
        }
    }

    /**
     * 处理命令列表
     * @param {Array} baseCommands - 后端返回的基础命令列表
     * @returns {Array} 处理后的完整命令列表（包含变体）
     */
    function processCommandList(baseCommands) {
        // 定义需要生成变体的命令及其变体后缀
        const commandsWithVariants = {
            'md': { variants: ['.b', '.w', '.l'] },
            'mw': { variants: ['.b', '.w', '.l'] },
            'cp': { variants: ['.b', '.w', '.l'] },
            'cmp': { variants: ['.b', '.w', '.l'] },
            'itest': { variants: ['.b', '.w', '.l', '.s'] }
        };

        // 创建基础命令名称的 Set 用于快速查找
        const baseCommandNames = new Set(baseCommands.map(cmd => cmd.name));

        // 构建最终的命令列表
        const allCommands = [...baseCommands];

        // 为存在的命令生成变体
        for (const [baseCmd, config] of Object.entries(commandsWithVariants)) {
            if (baseCommandNames.has(baseCmd)) {
                // 找到原始命令对象以获取其 usage
                const originalCmd = baseCommands.find(cmd => cmd.name === baseCmd);
                const originalUsage = originalCmd ? originalCmd.usage : "";

                // 为每个变体后缀生成命令
                for (const variant of config.variants) {
                    const variantName = baseCmd + variant;
                    // 避免重复添加（如果后端已经返回了变体命令）
                    if (!baseCommandNames.has(variantName)) {
                        allCommands.push({
                            name: variantName,
                            usage: originalUsage + " (" + variant.substring(1) + ")"
                        });
                    }
                }
            }
        }

        // 按名称排序
        allCommands.sort((a, b) => a.name.localeCompare(b.name));

        return allCommands;
    }

    /**
     * 加载命令列表
     */
    async function loadCommands() {
        if (state.isCommandLoaded || state.loadingCommands) return;

        state.loadingCommands = true;

        try {
            const response = await fetch("/webterm/cmdlist", {
                method: "GET",
                cache: "no-store",
                headers: {
                    "Accept": "application/json"
                }
            });

            if (!response.ok) {
                console.warn(`HTTP ${response.status}: Failed to load commands`);
                return;
            }

            const data = await response.json();

            if (data && data.cmdlist && Array.isArray(data.cmdlist)) {
                // 基础命令列表
                const baseCommands = data.cmdlist.map(cmd => ({
                    name: cmd.name,
                    usage: cmd.usage || ""
                }));

                state.commands = processCommandList(baseCommands);

                // 构建命令名称映射（用于快速精确匹配）
                state.commandNames.clear();
                state.commands.forEach(cmd => {
                    if (cmd.name) {
                        state.commandNames.set(cmd.name.toLowerCase(), cmd);
                    }
                });

                state.isCommandLoaded = true;

            } else {
                console.warn("Invalid commands data format: ", data);
            }

        } catch (error) {
            console.warn("Failed to load commands: ", error.message);
        } finally {
            state.loadingCommands = false;
        }
    }

    /**
     * 高亮匹配文本
     */
    function highlightMatch(text, query) {
        if (!query || query.length === 0) return text;

        const regex = new RegExp(`(${escapeRegex(query)})`);
        return text.replace(regex, match => `<span class="suggestion-highlight">${escapeHtml(match)}</span>`);
    }

    /**
     * 转义正则表达式特殊字符
     */
    function escapeRegex(str) {
        return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }

    /**
     * 获取命令的简短描述（从 usage 中提取）
     */
    function getCommandDescription(cmd) {
        if (!cmd.usage) return "";

        // 取 usage 的第一行，去除开头的特殊字符
        let firstLine = cmd.usage.split('\n')[0].trim();

        // 如果第一行只是命令名，尝试取第二行
        if (firstLine === cmd.name || firstLine === cmd.name + " ") {
            const lines = cmd.usage.split('\n');
            if (lines.length > 1) {
                firstLine = lines[1].trim();
            }
        }

        // 限制描述长度
        if (firstLine.length > 40) {
            firstLine = firstLine.substring(0, 37) + "...";
        }

        return firstLine;
    }

    /**
     * 从输入中提取命令名（忽略前导空格，只取第一个单词）和参数
     * @param {string} rawInput - 原始用户输入
     * @returns {Object} 提取的命令名（小写）和参数
     */
    function extractCommandAndArgument(rawInput) {
        let rawCommand = "", rawArgument = "";

        // 去除前导空格
        const trimmedInput = rawInput ? rawInput.trimStart() : "";
        if (trimmedInput.length !== 0) {
            // 提取第一个单词（命令名），遇到空格或换行停止
            const match = trimmedInput.match(/^[^\s]+/);
            rawCommand = match ? match[0].toLowerCase() : "";
            rawArgument = trimmedInput.substring(rawCommand.length);
        }

        return {
            rawCommand: rawCommand,
            rawArgument: rawArgument
        };
    }

    /**
     * 过滤匹配的命令
     * @param {string} input - 用户输入（可能包含参数）
     * @returns {Object} 匹配的命令列表及精确匹配信息
     */
    function filterCommands(input) {
        // 提取命令名（忽略前导空格和参数）
        const { rawCommand } = extractCommandAndArgument(input);

        // 如果命令名为空（输入为空或只有空格），返回空数组
        if (!rawCommand || rawCommand.length === 0) {
            return { matches: [], exactMatch: null };
        }

        const matches = state.commands.filter(cmd => {
            const cmdName = cmd.name.toLowerCase();
            // 前缀匹配
            if (cmdName.startsWith(rawCommand)) {
                return true;
            }
            return false;
        }).sort((a, b) => a.name.localeCompare(b.name));

        // 限制最多显示10条建议
        const limitedMatches = matches.slice(0, 10);

        // 检查是否有精确匹配
        let exactMatch = null;
        if (state.commandNames.has(rawCommand)) {
            exactMatch = state.commandNames.get(rawCommand);
        }

        // 检查是否有非精确匹配
        const hasInexactMatches = limitedMatches.some(cmd => cmd.name.toLowerCase() !== rawCommand);

        return {
            matches: limitedMatches,
            exactMatch: exactMatch,
            hasInexactMatches: hasInexactMatches
        };
    }

    /**
     * 显示提示框
     */
    function showSuggestions(filterResult, input) {
        const els = getElements();
        const inputEl = els.input;

        if (!els.suggestionsBox || !els.suggestionsList) return;

        // 提取命令名用于显示（去除前导空格）
        const { rawCommand, rawArgument } = extractCommandAndArgument(input);

        // 清除输入框的错误样式
        inputEl.classList.remove('input-error');

        // 如果输入为空（去除前导空格后），隐藏提示框
        if (!rawCommand || rawCommand.length === 0) {
            hideSuggestions();
            return;
        }

        const { matches, exactMatch, hasInexactMatches } = filterResult;

        // 判断需要隐藏提示框的条件（同时满足）：
        // 1. 有精确匹配
        // 2. 无非精确匹配或命令后有其他内容（即参数，空格也算）
        if (exactMatch && (!hasInexactMatches || rawArgument)) {
            hideSuggestions();
            return;
        }

        // 无匹配命令，显示错误提示
        if (matches.length === 0 && !exactMatch) {
            els.suggestionsBox.className = "cmd-suggestions no-match";
            els.suggestionsList.innerHTML = `
                <div class="no-match-message">
                    ${escapeHtml(rawCommand) + ": " + t("webterm.cmd.nomatch")}
                </div>
            `;
            els.suggestionsBox.style.display = "block";

            // 输入框中的文字变红
            inputEl.classList.add('input-error');
            return;
        }

        // 有匹配命令（包括精确匹配但还有其他选项的情况）
        if (matches.length > 0) {
            els.suggestionsBox.className = "cmd-suggestions";

            let html = '';
            matches.forEach(match => {
                const isExact = exactMatch && match.name.toLowerCase() === rawCommand;
                const displayDesc = getCommandDescription(match);
                const highlightedName = highlightMatch(escapeHtml(match.name), rawCommand);

                let itemClass = 'suggestion-item';
                if (isExact) {
                    itemClass += ' suggestion-exact';
                }

                html += `
                    <div class="${itemClass}" data-cmd="${escapeHtml(match.name)}">
                        <span class="suggestion-cmd">
                            <span class="suggestion-cmd-name">${highlightedName}</span>
                            ${isExact ? '<span class="suggestion-exact-badge">✓</span>' : ''}
                        </span>
                        <span class="suggestion-desc">${escapeHtml(displayDesc)}</span>
                    </div>
                `;
            });
            els.suggestionsList.innerHTML = html;
            els.suggestionsBox.style.display = "block";

            // 绑定点击事件
            const items = els.suggestionsList.querySelectorAll('.suggestion-item');
            items.forEach(item => {
                item.addEventListener('click', (e) => {
                    e.stopPropagation();

                    const cmd = item.getAttribute('data-cmd');
                    if (cmd && inputEl) {
                        // 保留用户已输入的命令后的参数部分
                        const { rawArgument } = extractCommandAndArgument(inputEl.value);

                        // 替换命令名，保留参数
                        inputEl.value = cmd + rawArgument;
                        inputEl.focus();
                        hideSuggestions();
                        inputEl.classList.remove('input-error');
                    }
                });
            });
            return;
        }

        // 默认情况（不应该到达这里）
        hideSuggestions();
    }

    /**
     * 隐藏提示框
     */
    function hideSuggestions() {
        const els = getElements();
        const inputEl = els.input;

        if (els.suggestionsBox) {
            els.suggestionsBox.style.display = "none";
        }

        // 清除错误样式
        if (inputEl) {
            inputEl.classList.remove('input-error');
        }

        state.currentMatch = null;
    }

    /**
     * 处理输入变化（防抖）
     */
    function onInputChange() {
        // 清除之前的定时器
        if (state.debounceTimer) {
            clearTimeout(state.debounceTimer);
        }

        // 设置防抖
        state.debounceTimer = setTimeout(() => {
            const inputEl = getElements().input;
            const rawInput = inputEl ? inputEl.value : "";

            // 如果命令列表还没加载，先加载
            if (!state.isCommandLoaded && !state.loadingCommands) {
                loadCommands().then(() => {
                    const filterResult = filterCommands(rawInput);
                    showSuggestions(filterResult, rawInput);
                });
            } else {
                const filterResult = filterCommands(rawInput);
                showSuggestions(filterResult, rawInput);
            }
        }, 200);
    }

    /**
     * 处理键盘事件（用于命令补全和发送）
     */
    function onKeyDown(e) {
        const els = getElements();
        const inputEl = els.input;
        if (!inputEl) return;

        // Enter 键发送命令
        if (e.key === 'Enter') {
            e.preventDefault();
            hideSuggestions();
            send();
            return;
        }

        // Tab 键自动补全
        if (e.key === 'Tab') {
            e.preventDefault();

            const rawInput = inputEl.value;
            const { rawCommand, rawArgument } = extractCommandAndArgument(rawInput);

            if (!rawCommand || rawCommand.length === 0) return;

            // 查找匹配的命令
            const filterResult = filterCommands(rawInput);
            const { matches } = filterResult;

            if (matches.length === 1) {
                // 只有一个匹配，自动补全
                hideSuggestions();
                inputEl.value = matches[0].name + rawArgument;
                inputEl.classList.remove('input-error');
            } else if (matches.length > 0) {
                // 多个匹配，显示提示框
                showSuggestions(filterResult, rawInput);
            }
            return;
        }

        // ESC 键隐藏提示框
        if (e.key === 'Escape') {
            hideSuggestions();
            return;
        }

        // 上下箭头历史记录（不处理 Tab 相关的）
        // 上箭头 - 历史记录向前
        if (e.key === "ArrowUp") {
            const history = state.history;
            if (!history || !history.length) return;

            state.histPos = Math.min(history.length - 1, state.histPos + 1);
            inputEl.value = history[state.histPos] || "";
            // 触发输入事件重新计算提示
            const newInput = inputEl.value;
            const filterResult = filterCommands(newInput);
            showSuggestions(filterResult, newInput);
            inputEl.classList.remove('input-error');
            e.preventDefault();
            return;
        }

        // 下箭头 - 历史记录向后
        if (e.key === "ArrowDown") {
            const history = state.history;
            if (!history || !history.length) return;

            state.histPos = Math.max(-1, state.histPos - 1);
            inputEl.value = state.histPos >= 0 ? (history[state.histPos] || "") : "";
            // 触发输入事件重新计算提示
            const newInput = inputEl.value;
            const filterResult = filterCommands(newInput);
            showSuggestions(filterResult, newInput);
            inputEl.classList.remove('input-error');
            e.preventDefault();
            return;
        }

        // Ctrl+L 清空终端输出
        if ((e.ctrlKey || e.metaKey) && e.key === "l") {
            e.preventDefault();
            clear();
        }
    }

    /**
     * 显示欢迎信息
     */
    function showWelcomeMessage() {
        addTerminalLine('system', 'Welcome to U-Boot Web Terminal');
        addTerminalLine('system', 'Type "help" to see available commands');
        addTerminalLine('system', 'Double-click the terminal area to focus on the input box');
        addTerminalLine('separator', '');
    }

    /**
     * 加载持久化的输出
     */
    function loadPersistedOutput() {
        const outputEl = getElements().output;
        if (!outputEl) return;

        try {
            const saved = sessionStorage.getItem(state.persistKey);
            if (saved) {
                outputEl.innerHTML = saved;
                // 滚动到底部
                outputEl.scrollTop = outputEl.scrollHeight;
            } else {
                showWelcomeMessage();
            }
        } catch (e) {
            console.warn("Failed to load persisted output:", e);
        }
    }

    /**
     * 保存输出到 sessionStorage
     */
    function savePersistedOutput() {
        const outputEl = getElements().output;
        if (!outputEl) return;

        try {
            let content = outputEl.innerHTML;
            if (content.length > state.persistMax) {
                content = content.slice(content.length - state.persistMax);
            }
            sessionStorage.setItem(state.persistKey, content);
        } catch (e) {
            console.warn("Failed to persist output:", e);
        }
    }

    /**
     * 添加一行到终端输出
     * @param {string} type - 行类型: 'command', 'output', 'error', 'system'
     * @param {string} content - 内容
     * @param {string} commandPrompt - 命令行的提示符（仅当 type='command' 时使用）
     */
    function addTerminalLine(type, content, commandPrompt = '$') {
        const els = getElements();
        const mainEl = els.main;
        const outputEl = els.output;

        if (!mainEl || !outputEl) return;

        const lineDiv = document.createElement('div');
        lineDiv.className = `terminal-line ${type}`;

        if (type === 'command') {
            const promptSpan = document.createElement('span');
            promptSpan.className = 'line-prompt';
            promptSpan.textContent = commandPrompt;
            const contentSpan = document.createElement('span');
            contentSpan.className = 'line-content';
            contentSpan.textContent = content;
            lineDiv.appendChild(promptSpan);
            lineDiv.appendChild(contentSpan);
        } else {
            const contentSpan = document.createElement('span');
            contentSpan.className = 'line-content';
            contentSpan.textContent = content;
            lineDiv.appendChild(contentSpan);
        }

        outputEl.appendChild(lineDiv);
        mainEl.scrollTop = mainEl.scrollHeight;
        savePersistedOutput();
    }

    /**
     * 格式化错误信息
     */
    function formatError(error) {
        if (!error) return t("error.unknown");
        if (error.message) return error.message;
        return String(error);
    }

    /**
     * 检查命令是否被禁止
     * @param {string} commandName - 命令名称
     * @returns {Object|null} 禁止信息，如果不被禁止则返回 null
     */
    function isForbiddenCommand(commandName) {
        if (!commandName) return null;
        const lowerName = commandName.toLowerCase();
        return state.forbiddenCommands.get(lowerName) || null;
    }

    /**
     * 检查并处理命令
     * @param {string} line - 输入的命令行
     * @returns {boolean} - 是否允许执行
     */
    function validateCommand(line) {
        const { rawCommand } = extractCommandAndArgument(line);
        const { matches, exactMatch } = filterCommands(line);
        let fullCommandName = "";

        if (exactMatch) {
            // 命令有精确匹配
            fullCommandName = rawCommand;
        } else if (matches.length === 1) {
            // 命令没有精确匹配，但有唯一前缀匹配
            fullCommandName = matches[0].name;
        } else {
            // 命令没有精确匹配，且无前缀匹配或有不止一个前缀匹配
            addTerminalLine('error', `${rawCommand}: ${t("webterm.cmd.unknown")}`);
            return false;
        }

        const forbiddenInfo = isForbiddenCommand(fullCommandName);
        if (forbiddenInfo) {
            addTerminalLine('error', `${fullCommandName}: ${t("webterm.cmd.forbid.hint")}`);
            if (forbiddenInfo.reasonKey) {
                addTerminalLine('error', t(forbiddenInfo.reasonKey));
            }
            if (forbiddenInfo.altKey) {
                addTerminalLine('system', t(forbiddenInfo.altKey));
            }
            return false;
        }

        return true;
    }

    /**
     * 处理用户输入
     */
    function processUserInput() {
        const inputEl = getElements().input;
        if (!inputEl) return null;

        const rawInput = inputEl.value;
        if (!rawInput) {
            addTerminalLine('command', '');
            return "__DO_REPEAT__";
        }

        inputEl.value = '';

        const trimmedInput = rawInput.trim();
        if (!trimmedInput) {
            addTerminalLine('command', '');
            return "__CANCEL_REPEAT__";
        }

        addTerminalLine('command', trimmedInput);
        hideSuggestions();

        if (!validateCommand(trimmedInput)) {
            setStatus(t("webterm.status.ready"));
            return "__CANCEL_REPEAT__";
        }

        state.history.unshift(trimmedInput);
        if (state.history.length > 50) {
            state.history.length = 50;
        }
        state.histPos = -1;

        return trimmedInput;
    }

    /**
     * 发送命令并执行
     */
    async function send() {
        const cmdLine = processUserInput();
        if (!cmdLine) return;

        focusInput();

        try {
            const formData = new FormData();
            formData.append("cmd", cmdLine);

            setStatus(t("webterm.status.running"));

            const response = await fetch("/webterm/exec", {
                method: "POST",
                body: formData
            });

            const text = await response.text();

            if (!response.ok) {
                const errorMsg = `${t("webterm.status.http")} ${response.status}${text ? ": " + text : ""}`;
                addTerminalLine('error', errorMsg);
                setStatus(errorMsg, true);
                return;
            }

            if (text && text.trim()) {
                // 移除末尾的换行符，但保留格式
                let outputText = text;
                if (outputText.endsWith('\n')) {
                    outputText = outputText.slice(0, -1);
                }
                // 分割多行输出
                const lines = outputText.split('\n');
                for (const lineText of lines) {
                    if (lineText.trim() !== "" || lines.length === 1) {
                        addTerminalLine('output', lineText);
                    } else if (lineText === "") {
                        addTerminalLine('output', "");
                    }
                }
            }

            setStatus(t("webterm.status.done"));

        } catch (error) {
            const errorMsg = formatError(error);
            addTerminalLine('error', errorMsg);
            setStatus(errorMsg, true);
        }
    }

    /**
     * 清空输出
     */
    async function clear() {
        const outputEl = getElements().output;
        if (outputEl) {
            outputEl.innerHTML = "";
            showWelcomeMessage();
            try {
                sessionStorage.removeItem(state.persistKey);
            } catch (e) {
                // 忽略清理错误
            }
            setStatus(t("webterm.status.cleared"));
        }

        focusInput();
    }

    /**
     * 文件上传功能
     */
    function uploadFile() {
        // 创建一个隐藏的文件选择器
        const fileInput = document.createElement("input");
        fileInput.type = "file";
        fileInput.style.display = "none";
        fileInput.accept = "*/*";

        fileInput.onchange = async function() {
            const file = fileInput.files[0];
            if (!file) return;

            showFileInfo(`${t("webterm.uploading")} ${file.name}`);
            showFileProgress(true);
            setFileProgress(0);

            const formData = new FormData();
            formData.append("file", file);

            try {
                const xhr = new XMLHttpRequest();

                xhr.upload.addEventListener("progress", function(event) {
                    if (event.lengthComputable && event.total > 0) {
                        const percent = parseInt((event.loaded / event.total) * 100);
                        setFileProgress(percent);
                        showFileInfo(`${t("webterm.uploading")} ${file.name} (${percent}%)`);
                    }
                });

                xhr.onreadystatechange = function() {
                    if (xhr.readyState === 4) {
                        showFileProgress(false);

                        if (xhr.status === 200) {
                            const text = xhr.responseText;
                            if (text) {
                                addTerminalLine('output', text);
                            }
                            showFileInfo(`✓ ${t("webterm.upload.success")}`, true);
                            setStatus(t("webterm.upload.success"));
                        } else {
                            showFileInfo(`✗ ${t("webterm.status.http")} ${xhr.status}`, false);
                            setStatus(`${t("webterm.status.http")} ${xhr.status}`, true);
                        }
                    }
                };

                xhr.open("POST", "/webterm/upload");
                xhr.send(formData);

            } catch (error) {
                showFileProgress(false);
                showFileInfo(`✗ ${formatError(error)}`, false);
                setStatus(formatError(error), true);
            }
        };

        // 触发文件选择
        document.body.appendChild(fileInput);
        fileInput.click();
        document.body.removeChild(fileInput);

        focusInput();
    }

    /**
     * 聚焦输入框
     */
    function focusInput() {
        const inputEl = getElements().input;
        if (inputEl) {
            inputEl.focus();
        }
    }

    /**
     * 绑定键盘事件
     */
    function bindKeyboardEvents() {
        const els = getElements();
        const mainEl = els.main;
        const inputEl = els.input;

        if (!mainEl || !inputEl) return;

        // 鼠标双击 main 区域，聚焦输入框
        mainEl.addEventListener("dblclick", focusInput);

        // 输入事件（带防抖）
        inputEl.addEventListener("input", onInputChange);

        // 键盘事件（统一处理所有按键）
        inputEl.addEventListener("keydown", onKeyDown);

        // 失去焦点时隐藏提示框（延迟一下，让点击事件有机会执行）
        inputEl.addEventListener("blur", () => {
            setTimeout(() => {
                hideSuggestions();
            }, 200);
        });

        // 获得焦点时的处理
        inputEl.addEventListener("focus", () => {
            const rawInput = inputEl.value;
            if (rawInput && rawInput.trimStart().length > 0) {
                if (!state.isCommandLoaded && !state.loadingCommands) {
                    loadCommands().then(() => {
                        const filterResult = filterCommands(rawInput);
                        showSuggestions(filterResult, rawInput);
                    });
                } else {
                    const filterResult = filterCommands(rawInput);
                    showSuggestions(filterResult, rawInput);
                }
            }
        });
    }

    /**
     * 初始化控制台管理器
     */
    async function init() {
        getElements();

        bindKeyboardEvents();

        loadPersistedOutput();

        // 预加载命令列表（不阻塞UI）
        loadCommands();

        setStatus(t("webterm.status.ready"));

        focusInput();
    }

    /**
     * 清理资源
     */
    function destroy() {
        if (state.debounceTimer) {
            clearTimeout(state.debounceTimer);
        }
        elements = null;
    }

    return {
        init,
        focusInput,
        send,
        clear,
        uploadFile,
        destroy
    };
})();

// ==============================
// 系统信息模块
// ==============================

/**
 * 系统信息管理器
 * 负责加载和渲染设备的系统信息、存储信息和分区表
 */
function renderRt3000StorageDevices(devices, table, renderDeviceInfo,
                                     createHeaderRow, createInfoRow,
                                     formatHexBytes, chinese) {
    if (!devices) return;
    const nor = devices.spi_nor;
    if (nor && nor.present === true) {
        const identity = nor.name || nor.model;
        const title = chinese ? 'SPI NOR \u95ea\u5b58' : 'SPI NOR Flash';
        createHeaderRow(table, 2, identity ? title + ' (' + identity + ')' : title);
        if (nor.name) createInfoRow(table, 'sysinfo.name', nor.name);
        if (nor.model && nor.model !== nor.name)
            createInfoRow(table, 'sysinfo.model', nor.model);
        if (nor.size !== undefined && nor.size !== null)
            createInfoRow(table, 'sysinfo.size', formatHexBytes(nor.size));
        for (const [field, label] of [
            ['page_size', 'sysinfo.page_size'],
            ['sector_size', 'sysinfo.sector_size'],
            ['erase_size', 'sysinfo.erase_size'],
            ['block_size', 'sysinfo.block_size']
        ]) {
            if (nor[field] !== undefined && nor[field] !== null)
                createInfoRow(table, label, formatHexBytes(nor[field]));
        }
        if (nor.bus !== undefined && nor.bus !== null)
            createInfoRow(table, 'sysinfo.bus', nor.bus);
        if (nor.chip_select !== undefined && nor.chip_select !== null)
            createInfoRow(table, 'sysinfo.chip_select', nor.chip_select);
    } else if (!nor && devices.spi && devices.spi.present) {
        renderDeviceInfo(devices.spi, 'spi', table);
    }
    if (devices.mmc && devices.mmc.present)
        renderDeviceInfo(devices.mmc, 'mmc', table);
    if (devices.nand && devices.nand.present)
        renderDeviceInfo(devices.nand, 'nand', table);
}

function appendSysinfoBoardRows(table, board, createInfoRow, formatHexBytes) {
    if (board.hostname) {
        const fromBuild = board.identity_source === "build_configuration";
        const nameLabel = fromBuild ? "sysinfo.build_name" : "sysinfo.hostname";
        const displayName = fromBuild && board.model ? board.model : board.hostname;
        table.appendChild(createInfoRow(nameLabel, displayName));
    }
    if (board.family) table.appendChild(createInfoRow("sysinfo.family", board.family));
    if (board.model && board.family !== "HW-B") table.appendChild(createInfoRow("sysinfo.model", board.model));
    if (board.compatible && board.family !== "HW-B") table.appendChild(createInfoRow("sysinfo.compatible", board.compatible));
    if (board.machid) table.appendChild(createInfoRow("sysinfo.machid", board.machid));
    if (board.ram_size !== undefined) {
        table.appendChild(createInfoRow("sysinfo.ram_size", formatHexBytes(board.ram_size)));
    }
}

const sysinfoManager = (() => {
    let elements = null;
    const sectionIds = ["board_info", "flash_info", "partitions_info"];

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {};
        sectionIds.forEach(function(id) {
            elements[id] = document.getElementById(id);
        });

        return elements;
    }

    /**
     * 创建表头行
     * @param {string} colSpan - 该行占用列数
     * @param {string} value - 显示的值
     * @returns {HTMLTableRowElement}
     */
    function createHeaderRow(colSpan, value) {
        const tr = document.createElement("tr");
        tr.className = "tr";

        const td = document.createElement("td");
        td.className = "td left";
        td.setAttribute("colspan", colSpan);
        td.style.fontWeight = "600";
        td.style.color = "var(--primary)";
        td.style.backgroundColor = "var(--panel-2)";
        td.textContent = value !== undefined && value !== null
            ? String(value)
            : t("sysinfo.no_data");

        tr.appendChild(td);
        return tr;
    }

    /**
     * 创建表格行
     * @param {string} labelKey - 标签的国际化 key
     * @param {string} value - 显示的值
     * @param {string} [valueClass] - 值的额外 CSS 类
     * @returns {HTMLTableRowElement}
     */
    function createInfoRow(labelKey, value, valueClass) {
        const tr = document.createElement("tr");
        tr.className = "tr";

        const tdLabel = document.createElement("td");
        tdLabel.className = "td left";
        tdLabel.setAttribute("width", "33%");
        tdLabel.setAttribute("data-i18n", labelKey);
        tdLabel.textContent = t(labelKey);

        const tdValue = document.createElement("td");
        tdValue.className = "td left";
        if (valueClass) {
            tdValue.classList.add(valueClass);
        }
        tdValue.textContent = value !== undefined && value !== null
            ? String(value)
            : t("sysinfo.no_data");

        tr.appendChild(tdLabel);
        tr.appendChild(tdValue);
        return tr;
    }

    /**
     * 创建板块表格容器
     * @param {string} sectionId - 板块元素 ID
     * @param {string} titleKey - 标题的国际化 key
     * @returns {HTMLTableElement|null}
     */
    function createSectionTable(sectionId, titleKey) {
        const section = document.getElementById(sectionId);
        if (!section) return null;

        const inner = section.querySelector(".card-main-inner");
        if (!inner) return null;

        inner.innerHTML = "";

        const h2 = document.createElement("h2");
        h2.setAttribute("data-i18n", titleKey);
        h2.textContent = t(titleKey);
        inner.appendChild(h2);

        const table = document.createElement("table");
        table.className = "table";
        inner.appendChild(table);

        return table;
    }

    /**
     * 格式化字节数为可读字符串
     * @param {number} bytes
     * @returns {string}
     */
    function formatHexBytes(bytes) {
        if (bytes === undefined || bytes === null) return t("sysinfo.no_data");
        const hex = "0x" + bytes.toString(16).toUpperCase();
        const human = bytesToHuman(bytes);
        return hex + " (" + human + ")";
    }

    /**
     * 创建 eMMC 寿命信息表格行
     * @param {string} label - 标签文本
     * @param {string} value - 显示的值
     * @returns {HTMLTableRowElement}
     */
    function createLifeInfoRow(label, value) {
        const tr = document.createElement("tr");
        tr.className = "tr";

        const tdLabel = document.createElement("td");
        tdLabel.className = "td left";
        tdLabel.setAttribute("width", "33%");
        tdLabel.textContent = label || "";

        const tdValue = document.createElement("td");
        tdValue.className = "td left life-value-cell";
        tdValue.innerHTML = value !== undefined && value !== null
            ? String(value)
            : t("sysinfo.no_data");

        tr.appendChild(tdLabel);
        tr.appendChild(tdValue);
        return tr;
    }

    /**
     * 解析 eMMC 寿命值
     * @param {string} value - 原始值 (如 "0x01")
     * @returns {object} { range: string, percent: number, score: number }
     */
    function parseLifeValue(value) {
        if (!value) return { range: '------', percent: 0, score: 0 };

        const val = value.trim().toLowerCase();
        const mapping = {
            '0x00': { range: '------', percent: 0, score: 0 },
            '0x01': { range: '0-10%', percent: 5, score: 1 },
            '0x02': { range: '10-20%', percent: 15, score: 2 },
            '0x03': { range: '20-30%', percent: 25, score: 3 },
            '0x04': { range: '30-40%', percent: 35, score: 4 },
            '0x05': { range: '40-50%', percent: 45, score: 5 },
            '0x06': { range: '50-60%', percent: 55, score: 6 },
            '0x07': { range: '60-70%', percent: 65, score: 7 },
            '0x08': { range: '70-80%', percent: 75, score: 8 },
            '0x09': { range: '80-90%', percent: 85, score: 9 },
            '0x0a': { range: '90-100%', percent: 95, score: 10 },
            '0x0b': { range: '100%', percent: 100, score: 11 },
        };

        return mapping[val] || { range: '------', percent: 0, score: 0 };
    }

    /**
     * 获取 PRE_EOL 等级
     * @param {string} value - 原始值 (如 "0x01")
     * @returns {string}
     */
    function getPreEolLevel(value) {
        if (!value) return 'unknown';

        const val = value.trim().toLowerCase();
        const mapping = {
            '0x01': 'normal',
            '0x02': 'warning',
            '0x03': 'urgent',
        };

        return mapping[val] || 'unknown';
    }

    /**
     * 创建寿命进度条 HTML
     * @param {number} percent - 百分比 (0-100)
     * @param {number} percentRange - 百分比范围
     * @returns {string} HTML 字符串
     */
    function createLifeProgressBar(percent, percentRange) {
        const clampedPercent = Math.max(0, Math.min(100, percent));
        return `
            <div class="life-progress-container">
                <div class="life-progress-track">
                    <div class="life-progress-bar" style="width: ${clampedPercent}%;"></div>
                </div>
                <span class="life-progress-percent">${escapeHtml(percentRange)}</span>
            </div>
        `;
    }

    /**
     * 渲染 eMMC 寿命信息
     * @param {object} mmcDevice - MMC 设备对象
     * @param {HTMLTableElement} table - 目标表格
     */
    function renderLifeInfo(mmcDevice, table) {
        if (!mmcDevice || (!mmcDevice.life_a && !mmcDevice.life_b && !mmcDevice.pre_eol)) {
            return;
        }

        // 添加分隔行
        table.appendChild(createHeaderRow(2, t('sysinfo.emmc_life')));

        // Life Time A
        if (mmcDevice.life_a) {
            const lifeA = parseLifeValue(mmcDevice.life_a);
            const lifeAHtml = `
                ${createLifeProgressBar(lifeA.percent, lifeA.range)}
                <span class="life-raw-value">${escapeHtml(mmcDevice.life_a)}</span>
            `;
            table.appendChild(createLifeInfoRow('Life Time A', lifeAHtml));
        }

        // Life Time B
        if (mmcDevice.life_b) {
            const lifeB = parseLifeValue(mmcDevice.life_b);
            const lifeBHtml = `
                ${createLifeProgressBar(lifeB.percent, lifeB.range)}
                <span class="life-raw-value">${escapeHtml(mmcDevice.life_b)}</span>
            `;
            table.appendChild(createLifeInfoRow('Life Time B', lifeBHtml));
        }

        // Pre-EOL
        if (mmcDevice.pre_eol) {
            const preEolLevel = getPreEolLevel(mmcDevice.pre_eol);
            const statusClass = 'life-status-' + preEolLevel;
            const statusKey = 'sysinfo.status.' + preEolLevel;
            const preEolHtml = `
                <span class="life-status ${statusClass}" data-i18n="${statusKey}">${t(statusKey)}</span>
                <span class="life-raw-value">${escapeHtml(mmcDevice.pre_eol)}</span>
            `;
            table.appendChild(createLifeInfoRow('Pre-EOL Status', preEolHtml));
        }
    }

    /**
     * 处理设备信息
     * @param {object} device - 设备对象
     * @param {string} type - 设备类型 (spi/mmc/nand)
     * @param {HTMLTableElement} table - 目标表格
     */
    function renderDeviceInfo(device, type, table) {
        if (!device || !device.present) return;

        // 添加设备类型标题行
        let deviceName;
        if (type === "mmc" && device.vendor && device.product) {
            deviceName = device.vendor + " / " + device.product;
        } else if (device.name) {
            deviceName = device.name;
        } else {
            deviceName = t("sysinfo.present");
        }

        const headerValue = type.toUpperCase() + " (" + deviceName + ")";
        table.appendChild(createHeaderRow(2, headerValue));

        // 大小
        if (device.size !== undefined) {
            table.appendChild(createInfoRow("sysinfo.size", formatHexBytes(device.size)));
        }

        // 根据设备类型显示不同属性
        const propsMap = {
            spi: {
                "page_size": "sysinfo.page_size",
                "block_size": "sysinfo.block_size",
                "sector_size": "sysinfo.sector_size",
                "erase_size": "sysinfo.erase_size",
            },
            mmc: {
                "block_size": "sysinfo.block_size",
                "version": "sysinfo.version",
            },
            nand: {
                "model": "sysinfo.model",
                "page_size": "sysinfo.page_size",
                "block_size": "sysinfo.block_size",
                "oob_size": "sysinfo.oob_size",
                "oob_avail": "sysinfo.oob_avail",
                "ecc_step_size": "sysinfo.ecc_step_size",
                "ecc_strength": "sysinfo.ecc_strength",
            },
        };

        const propsToShow = propsMap[type] || {};

        for (const [prop, labelKey] of Object.entries(propsToShow)) {
            if (device[prop] !== undefined && !(prop === "model" && device[prop] === null)) {
                let value = device[prop];
                // 大小相关的用十六进制显示
                if (prop.includes("size")) {
                    value = formatHexBytes(value);
                }
                table.appendChild(createInfoRow(labelKey, value));
            }
        }

        if (type === 'mmc') {
            renderLifeInfo(device, table);
        }
    }

    /**
     * 渲染分区信息
     * @param {object} partitionsInfo - 分区数据
     * @param {HTMLTableElement} table - 目标表格
     */
    function renderPartitions(partitionsInfo, table) {
        if (!partitionsInfo) return;

        const partTypes = ["smem", "mmc"];

        partTypes.forEach(function(partType) {
            const partData = partitionsInfo[partType];
            if (!partData || !partData.present || !partData.parts || !partData.parts.length) {
                return;
            }

            const headerValue = partType.toUpperCase() + " " + t("sysinfo.title.partitions_info");
            table.appendChild(createHeaderRow(5, headerValue));

            // 添加表头行
            const theadTr = document.createElement("tr");
            theadTr.className = "tr";

            const columns = [
                { key: "sysinfo.part_index", width: "8%" },
                { key: "sysinfo.name", width: "20%" },
                { key: "sysinfo.part_start", width: "24%" },
                { key: "sysinfo.part_end", width: "24%" },
                { key: "sysinfo.size", width: "24%" }
            ];

            columns.forEach(function(col) {
                const th = document.createElement("td");
                th.className = "td left";
                th.setAttribute("width", col.width);
                th.setAttribute("data-i18n", col.key);
                th.textContent = t(col.key);
                th.style.fontWeight = "500";
                th.style.fontSize = "0.85rem";
                th.style.color = "var(--muted)";
                theadTr.appendChild(th);
            });

            table.appendChild(theadTr);

            // 添加每个分区
            partData.parts.forEach(function(part, index) {
                const tr = document.createElement("tr");
                tr.className = "tr";

                const cellStyle = "font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 0.85rem;";

                // 序号（从 1 开始）
                const tdIndex = document.createElement("td");
                tdIndex.className = "td left";
                tdIndex.setAttribute("width", "8%");
                tdIndex.textContent = index + 1;
                tdIndex.style.cssText = cellStyle;

                // 分区名
                const tdName = document.createElement("td");
                tdName.className = "td left";
                tdName.setAttribute("width", "20%");
                tdName.textContent = part.name || "";
                tdName.style.cssText = cellStyle;

                // 起始地址
                const tdStart = document.createElement("td");
                tdStart.className = "td left";
                tdStart.setAttribute("width", "24%");
                const start = part.start || 0;
                tdStart.textContent = "0x" + start.toString(16).toUpperCase();
                tdStart.style.cssText = cellStyle;

                // 结束地址 = 起始地址 + 大小 - 1
                const tdEnd = document.createElement("td");
                tdEnd.className = "td left";
                tdEnd.setAttribute("width", "24%");
                const size = part.size || 0;
                const end = start + size - 1;
                tdEnd.textContent = "0x" + end.toString(16).toUpperCase();
                tdEnd.style.cssText = cellStyle;

                // 大小
                const tdSize = document.createElement("td");
                tdSize.className = "td left";
                tdSize.setAttribute("width", "24%");
                tdSize.textContent = "0x" + size.toString(16).toUpperCase() + " (" + bytesToHuman(size) + ")";
                tdSize.style.cssText = cellStyle;

                tr.appendChild(tdIndex);
                tr.appendChild(tdName);
                tr.appendChild(tdStart);
                tr.appendChild(tdEnd);
                tr.appendChild(tdSize);
                table.appendChild(tr);
            });
        });
    }

    /**
     * 渲染系统信息数据
     * @param {object} data - 解析后的系统信息数据
     */
    function renderData(data) {
        // ========== 设备信息 ==========
        const boardTable = createSectionTable("board_info", "sysinfo.title.board_info");
        if (boardTable) {
            if (data.board) {
                appendSysinfoBoardRows(boardTable, data.board, createInfoRow, formatHexBytes);
            }

            // 启动模式
            if (data.is_9008_mode !== undefined) {
                const bootMode = data.is_9008_mode ? "9008" : "Normal";
                boardTable.appendChild(createInfoRow("sysinfo.boot_mode", bootMode));
            }

            // U-Boot 版本
            if (data.uboot_version) {
                boardTable.appendChild(createInfoRow("sysinfo.uboot_version", data.uboot_version));
            }
        }

        // ========== 存储信息 ==========
        const flashTable = createSectionTable("flash_info", "sysinfo.title.flash_info");
        if (flashTable) {
            // SMEM 信息
            if (data.smeminfo) {
                flashTable.appendChild(createHeaderRow(2, t("sysinfo.smeminfo")));

                const smem = data.smeminfo;
                if (smem.flash_type) flashTable.appendChild(createInfoRow("sysinfo.flash_type", smem.flash_type));
                if (smem.flash_block_size) flashTable.appendChild(createInfoRow("sysinfo.flash_block_size", formatHexBytes(smem.flash_block_size)));
                if (smem.flash_density) flashTable.appendChild(createInfoRow("sysinfo.flash_density", formatHexBytes(smem.flash_density)));
            }

            // 设备信息
            if (data.devices) {
                renderRt3000StorageDevices(
                    data.devices, flashTable, renderDeviceInfo,
                    (table, span, value) => table.appendChild(createHeaderRow(span, value)),
                    (table, label, value) => table.appendChild(createInfoRow(label, value)),
                    formatHexBytes, APP_STATE.lang === "zh-cn");
            }

            // 如果没有任何设备，显示提示
            if (flashTable.querySelectorAll("tr").length === 0) {
                flashTable.appendChild(createInfoRow("sysinfo.no_data", t("sysinfo.no_data")));
            }
        }

        // ========== 分区信息 ==========
        const partitionTable = createSectionTable("partitions_info", "sysinfo.title.partitions_info");
        if (partitionTable) {
            if (data.partitions) {
                renderPartitions(data.partitions, partitionTable);
            }

            // 如果没有任何分区，显示提示
            if (partitionTable.querySelectorAll("tr").length === 0) {
                partitionTable.appendChild(createInfoRow("sysinfo.no_data", t("sysinfo.no_data")));
            }
        }

        // 应用国际化到所有板块
        sectionIds.forEach(function(id) {
            const el = document.getElementById(id);
            if (el) {
                applyI18n(el);
            }
        });
    }

    /**
     * 显示加载状态
     */
    function showLoading() {
        sectionIds.forEach(function(id) {
            const el = document.getElementById(id);
            if (el) {
                const inner = el.querySelector(".card-main-inner");
                if (inner) {
                    inner.innerHTML = '<span style="color: var(--muted);">' +
                        t("sysinfo.loading") + '</span>';
                }
            }
        });
    }

    /**
     * 显示错误状态
     * @param {string} errorKey - 错误信息的国际化 key
     */
    function showError(errorKey) {
        sectionIds.forEach(function(id) {
            const el = document.getElementById(id);
            if (el) {
                const inner = el.querySelector(".card-main-inner");
                if (inner) {
                    inner.innerHTML = '<span style="color: var(--danger);">' +
                        t(errorKey) + '</span>';
                }
            }
        });
    }

    /**
     * 加载系统信息
     */
    function load() {
        showLoading();

        ajax({
            url: "/sysinfo",
            timeout: 10000,
            done: function(responseText) {
                let data;
                try {
                    data = JSON.parse(responseText);
                } catch (e) {
                    showError("sysinfo.error.parse");
                    return;
                }

                renderData(data);
            }
        });
    }

    /**
     * 获取系统信息并存储在全局状态中
     * 供其他模块使用（如 MIBIB 重载模块需要检查 9008 模式）
     */
    function fetchAndStore() {
        ajax({
            url: "/sysinfo",
            timeout: 10000,
            done: function(responseText) {
                try {
                    APP_STATE.sysinfo = JSON.parse(responseText);
                } catch (e) {
                    // 静默失败，其他模块会检查 APP_STATE.sysinfo
                }
            }
        });
    }

    /**
     * 初始化系统信息管理器
     */
    async function init() {
        load();
    }

    return {
        init,
        load,
        fetchAndStore,
    };
})();

// ==============================
// 系统日志模块
// ==============================

/**
 * 系统日志管理器
 * 负责通过轮询方式获取系统日志
 */
const syslogManager = (() => {
    let elements = null;
    let state = {
        running: false,
        pollTimer: null,
        autoScroll: true,
        persistKey: "failsafe_syslog_output",
        persistMax: 500000,  // 最大存储 500KB
        logBuffer: ""
    };

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            output: document.getElementById("syslog_out"),
            status: document.getElementById("syslog_status")
        };

        return elements;
    }

    /**
     * 设置状态提示
     */
    function setStatus(text, isError) {
        const el = getElements().status;
        if (el) {
            el.textContent = text || "";
            if (isError) {
                el.style.color = "var(--danger)";
            } else {
                el.style.color = "";
            }
        }

        // 如果是成功消息（非错误），3秒后自动清除
        if (!isError && text && text !== t("syslog.status.ready")) {
            setTimeout(() => {
                if (getElements().status && getElements().status.textContent === text) {
                    setStatus(t("syslog.status.ready"));
                }
            }, 3000);
        }
    }

    /**
     * 加载持久化的输出
     */
    function loadPersistedOutput() {
        const outputEl = getElements().output;
        if (!outputEl) return;

        try {
            const saved = sessionStorage.getItem(state.persistKey);
            if (saved) {
                outputEl.textContent = saved;
                state.logBuffer = saved;
                // 滚动到底部
                if (state.autoScroll) {
                    outputEl.scrollTop = outputEl.scrollHeight;
                }
            }
        } catch (e) {
            console.warn("Failed to load persisted syslog:", e);
        }
    }

    /**
     * 保存输出到 sessionStorage
     */
    function savePersistedOutput() {
        const outputEl = getElements().output;
        if (!outputEl) return;

        try {
            let content = outputEl.textContent || "";
            if (content.length > state.persistMax) {
                // 保留最近的内容
                content = content.slice(content.length - state.persistMax);
            }
            sessionStorage.setItem(state.persistKey, content);
            state.logBuffer = content;
        } catch (e) {
            console.warn("Failed to persist syslog:", e);
        }
    }

    /**
     * 追加日志到输出区域
     */
    function appendLog(text) {
        const outputEl = getElements().output;
        if (!outputEl || !text) return;

        // 直接追加文本，不进行 HTML 转义（保持原始格式）
        if (outputEl.textContent === "") {
            outputEl.textContent = text;
        } else {
            outputEl.textContent += text;
        }

        // 限制输出长度
        if (outputEl.textContent.length > state.persistMax) {
            outputEl.textContent = outputEl.textContent.slice(
                outputEl.textContent.length - state.persistMax
            );
        }

        savePersistedOutput();

        // 自动滚动到底部
        if (state.autoScroll) {
            outputEl.scrollTop = outputEl.scrollHeight;
        }
    }

    /**
     * 复制日志到剪贴板
     */
    async function copy() {
        const outputEl = getElements().output;
        if (!outputEl || !outputEl.textContent) {
            setStatus(t("syslog.status.no_content"), true);
            return;
        }

        try {
            const logText = outputEl.textContent;

            // 使用现代 Clipboard API
            if (navigator.clipboard && navigator.clipboard.writeText) {
                await navigator.clipboard.writeText(logText);
                setStatus(t("syslog.status.copied"));
            }
            // 降级方案：使用传统的 execCommand
            else {
                // 创建临时 textarea 元素
                const textarea = document.createElement("textarea");
                textarea.value = logText;
                textarea.style.position = "fixed";
                textarea.style.left = "-9999px";
                textarea.style.top = "-9999px";
                document.body.appendChild(textarea);

                textarea.select();
                textarea.setSelectionRange(0, logText.length);

                const success = document.execCommand("copy");
                document.body.removeChild(textarea);

                if (success) {
                    setStatus(t("syslog.status.copied"));
                } else {
                    throw new Error("execCommand failed");
                }
            }
        } catch (error) {
            console.error("Copy failed:", error);
            setStatus(t("syslog.status.copy_failed"), true);
        }
    }

    /**
     * 清空日志输出
     */
    function clear() {
        if (!confirm(t("syslog.confirm.clear"))) return;

        const outputEl = getElements().output;
        if (outputEl) {
            outputEl.textContent = "";
            state.logBuffer = "";
            try {
                sessionStorage.removeItem(state.persistKey);
            } catch (e) {
                // 忽略清理错误
            }
            setStatus(t("syslog.status.cleared"));
        }
    }

    /**
     * 格式化日期时间：年-月-日_时-分-秒
     */
    function formatDateTime(date) {
        const year = date.getFullYear();
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const day = String(date.getDate()).padStart(2, '0');
        const hours = String(date.getHours()).padStart(2, '0');
        const minutes = String(date.getMinutes()).padStart(2, '0');
        const seconds = String(date.getSeconds()).padStart(2, '0');

        return `${year}-${month}-${day}_${hours}-${minutes}-${seconds}`;
    }

    /**
     * 保存日志文件
     */
    function save() {
        const outputEl = getElements().output;
        if (!outputEl || !outputEl.textContent) {
            setStatus(t("syslog.status.no_content"), true);
            return;
        }

        try {
            // 获取原始文本
            let logText = outputEl.textContent || "";

            if (!logText) {
                setStatus(t("syslog.status.no_content"), true);
                return;
            }

            const now = new Date();
            const dateTimeStr = formatDateTime(now);
            const filename = `uboot_syslog_${dateTimeStr}.log`;

            const blob = new Blob([logText], { type: "text/plain;charset=utf-8" });
            const link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            URL.revokeObjectURL(link.href);

            setStatus(t("syslog.status.saved"));
        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 切换自动滚动
     */
    function toggleAutoScroll() {
        state.autoScroll = !state.autoScroll;
        const btn = document.querySelector('.syslog-actions .button-sm:nth-child(4)');
        if (btn) {
            if (state.autoScroll) {
                btn.classList.remove('button-inactive');
                btn.style.opacity = "1";
            } else {
                btn.classList.add('button-inactive');
                btn.style.opacity = "0.6";
            }
        }
        setStatus(state.autoScroll ? t("syslog.status.auto_scroll_on") : t("syslog.status.auto_scroll_off"));

        // 如果自动滚动开启，立即滚动到底部
        if (state.autoScroll) {
            const outputEl = getElements().output;
            if (outputEl) {
                outputEl.scrollTop = outputEl.scrollHeight;
            }
        }
    }

    /**
     * 格式化错误信息
     */
    function formatError(error) {
        if (!error) return t("error.unknown");
        if (error.message) return error.message;
        return String(error);
    }

    /**
     * 单次轮询获取日志
     */
    async function pollOnce() {
        if (!state.running) return;

        try {
            const url = '/syslog/poll';
            const response = await fetch(url, {
                method: "GET",
                cache: "no-store"
            });

            if (!response.ok) {
                if (response.status !== 404) {
                    setStatus(`${t("syslog.status.http")} ${response.status}`, true);
                }
                return;
            }

            const text = await response.text();
            if (text && text.trim()) {
                appendLog(text);
            }

        } catch (error) {
            console.warn("Syslog poll error:", error);
            setStatus(formatError(error), true);
        }
    }

    /**
     * 启动轮询调度
     */
    function schedulePoll() {
        if (state.pollTimer) {
            clearTimeout(state.pollTimer);
        }

        state.pollTimer = setTimeout(async () => {
            await pollOnce();
            schedulePoll();
        }, 500);  // 0.5 秒轮询间隔
    }

    /**
     * 停止轮询
     */
    function stopPoll() {
        if (state.pollTimer) {
            clearTimeout(state.pollTimer);
            state.pollTimer = null;
        }
        state.running = false;
    }

    /**
     * 初始化系统日志管理器
     */
    async function init() {
        getElements();

        loadPersistedOutput();

        // 启动轮询
        state.running = true;
        setStatus(t("syslog.status.ready"));
        schedulePoll();

        // 页面卸载时停止轮询
        window.addEventListener("beforeunload", () => {
            stopPoll();
        });
    }

    /**
     * 销毁管理器，清理资源
     */
    function destroy() {
        stopPoll();
        elements = null;
    }

    return {
        init,
        copy,
        clear,
        save,
        toggleAutoScroll,
        destroy
    };
})();

// ==============================
// 设置页面管理器
// ==============================

const settingsPageManager = (() => {
    let elements = null;

    function getElements() {
        if (elements) return elements;

        elements = {
            langSelect: document.getElementById("settings_lang"),
            themeSelect: document.getElementById("settings_theme")
        };

        return elements;
    }

    /**
     * 加载当前设置到表单
     */
    function loadSettingsToForm() {
        const els = getElements();

        if (els.langSelect) {
            els.langSelect.value = APP_STATE.lang || "en";
        }

        if (els.themeSelect) {
            els.themeSelect.value = APP_STATE.theme || "auto";
        }
    }

    /**
     * 绑定事件
     */
    function bindEvents() {
        const els = getElements();

        // 实时应用主题（选择即生效）
        if (els.themeSelect) {
            els.themeSelect.addEventListener("change", () => {
                setTheme(els.themeSelect.value);
            });
        }

        // 实时应用语言（选择即生效）
        if (els.langSelect) {
            els.langSelect.addEventListener("change", () => {
                setLang(els.langSelect.value);
                // 更新保存按钮文本（因为语言变了）
                if (els.saveBtn) {
                    els.saveBtn.textContent = t("settings.action.save");
                }
                if (els.resetBtn) {
                    els.resetBtn.textContent = t("settings.action.reset");
                }
            });
        }
    }

    /**
     * 初始化设置页面
     */
    async function init() {
        getElements();
        loadSettingsToForm();
        bindEvents();
    }

    return {
        init,
    };
})();

// ==============================
// MAC 地址管理模块
// ==============================

/**
 * MAC 地址管理器
 * 负责处理设备 MAC 地址的查看、修改等操作
 */
const macManager = (() => {
    let elements = null;
    let macList = [];           // 存储 MAC 地址数组
    let partName = "";
    let macWritable = false;
    let macEnvBase = false;          // 存储分区名称
    let refreshTimer = null;    // 延迟刷新定时器

    /**
     * 获取或缓存 DOM 元素
     */
    function getElements() {
        if (elements) return elements;

        elements = {
            status: document.getElementById("mac_status"),
            partNameDisplay: document.getElementById("mac_part_name"),
            tableBody: document.getElementById("mac_table_body"),
            tableContainer: document.getElementById("mac_table_container"),
            modifyBtn: document.getElementById("mac_modify_btn"),
            saveBtn: document.getElementById("mac_save_btn"),
            cancelBtn: document.getElementById("mac_cancel_btn"),
            actionGroup: document.getElementById("mac_action_group"),
            saveCancelGroup: document.getElementById("mac_save_cancel_group")
        };

        return elements;
    }

    /**
     * 设置状态提示
     */
    function setStatus(text, isError) {
        const el = getElements().status;
        if (el) {
            el.textContent = text || "";
            if (isError) {
                el.style.color = "var(--danger)";
            } else {
                el.style.color = "";
            }
        }
    }

    /**
     * 设置分区名称显示
     */
    function setPartName(name) {
        const el = getElements().partNameDisplay;
        if (el) {
            if (name) {
                el.textContent = name;
                el.style.display = "inline";
            } else {
                el.textContent = "";
                el.style.display = "none";
            }
        }
    }

    /**
     * 格式化错误信息
     */
    function setMacStoragePresentation(data) {
        const bEnvironment = !!data && (data.part_name === "0:APPSBLENV" ||
            data.source === "uboot_environment" || data.variable === "ethaddr");
        macEnvBase = bEnvironment && data.source === "uboot_environment" &&
            data.variable === "ethaddr" && data.part_name === "0:APPSBLENV";
        macWritable = bEnvironment
            ? macEnvBase && data.writable === true && data.persistent === true
            : !!data && data.writable !== false;
        const warningKeys = macEnvBase
            ? ["mac.warn.uboot.1", "mac.warn.uboot.2", "mac.warn.uboot.3", "mac.warn.uboot.4"]
            : ["mac.warn.1", "mac.warn.2", "mac.warn.3", "mac.warn.4"];
        document.querySelectorAll('[data-i18n^="mac.warn."]').forEach((item, index) => {
            if (index >= warningKeys.length) return;
            item.setAttribute("data-i18n", warningKeys[index]);
            item.textContent = t(warningKeys[index]);
        });
        const button = getElements().modifyBtn;
        if (button) button.disabled = !macWritable;
    }

    function formatError(error) {
        if (!error) return t("error.unknown");
        if (error.message) return error.message;
        return String(error);
    }

    /**
     * 验证单个 MAC 字节是否为有效的十六进制数（00-FF）
     * @param {string} byte - 两位十六进制字符串
     * @returns {boolean}
     */
    function isValidHexByte(byte) {
        if (!byte || byte.length < 1 || byte.length > 2) return false;
        const hexPattern = /^[0-9A-Fa-f]{1,2}$/;
        return hexPattern.test(byte);
    }

    /**
     * 验证完整的 MAC 地址
     * @param {Array<string>} bytes - 6个字节的数组
     * @returns {boolean}
     */
    function isValidMac(bytes) {
        if (!bytes || bytes.length !== 6) return false;
        return bytes.every(byte => isValidHexByte(byte));
    }

    /**
     * 将 MAC 地址字符串转换为字节数组
     * @param {string} mac - MAC 地址字符串 (格式: XX:XX:XX:XX:XX:XX)
     * @returns {Array<string>}
     */
    function macToBytes(mac) {
        if (!mac) return Array(6).fill("00");
        const parts = mac.split(':');
        if (parts.length === 6) {
            return parts.map(p => p.toUpperCase().padStart(2, '0'));
        }
        return Array(6).fill("00");
    }

    /**
     * 将字节数组转换为 MAC 地址字符串
     * @param {Array<string>} bytes - 6个字节的数组
     * @returns {string}
     */
    function isValidUnicastMac(bytes) {
        if (!isValidMac(bytes)) return false;
        const octets = bytes.map(byte => parseInt(byte, 16));
        return (octets[0] & 1) === 0 && octets.some(byte => byte !== 0);
    }

    function bytesToMac(bytes) {
        if (!bytes || bytes.length !== 6) return "";
        return bytes.map(b => b.toUpperCase().padStart(2, '0')).join(':');
    }

    /**
     * 获取所有 MAC 输入组的引用
     * @returns {NodeList} MAC 输入组元素列表
     */
    function getAllMacGroups() {
        return document.querySelectorAll(".mac-input-group");
    }

    /**
     * 获取当前聚焦的输入框所在的组和字节索引
     * @param {HTMLInputElement} currentInput - 当前输入框
     * @returns {Object|null} { group: HTMLElement, groupIndex: number, byteIndex: number }
     */
    function getInputContext(currentInput) {
        if (!currentInput) return null;

        const group = currentInput.closest(".mac-input-group");
        if (!group) return null;

        const groups = getAllMacGroups();
        let groupIndex = -1;
        for (let i = 0; i < groups.length; i++) {
            if (groups[i] === group) {
                groupIndex = i;
                break;
            }
        }
        if (groupIndex === -1) return null;

        const byteIndex = parseInt(currentInput.dataset.byteIndex);
        if (isNaN(byteIndex)) return null;

        return { group, groupIndex, byteIndex };
    }

    /**
     * 跳转到当前 MAC 内的相邻字节
     * @param {HTMLInputElement} currentInput - 当前输入框
     * @param {Array<HTMLInputElement>} allInputs - 当前 MAC 的所有输入框
     * @param {number} byteIndex - 当前字节索引
     * @param {number} direction - 方向：-1 表示上一个，1 表示下一个
     * @returns {boolean} 是否成功跳转
     */
    function navigateWithinMac(currentInput, allInputs, byteIndex, direction) {
        const targetIndex = byteIndex + direction;
        if (targetIndex < 0 || targetIndex > 5) return false;

        const targetInput = allInputs[targetIndex];
        if (targetInput && !targetInput.disabled) {
            targetInput.focus();
            targetInput.select();
            return true;
        }
        return false;
    }

    /**
     * 跳转到相邻 MAC 的指定字节
     * @param {HTMLInputElement} currentInput - 当前输入框
     * @param {number} groupOffset - 组偏移：-1 表示上一个 MAC，1 表示下一个 MAC
     * @param {number} targetByteIndex - 目标字节索引
     * @returns {boolean} 是否成功跳转
     */
    function navigateToAdjacentMac(currentInput, groupOffset, targetByteIndex) {
        const context = getInputContext(currentInput);
        if (!context) return false;

        const groups = getAllMacGroups();
        const targetGroupIndex = context.groupIndex + groupOffset;

        if (targetGroupIndex < 0 || targetGroupIndex >= groups.length) return false;

        const targetGroup = groups[targetGroupIndex];
        if (!targetGroup || !targetGroup.inputs) return false;

        const targetInput = targetGroup.inputs[targetByteIndex];
        if (targetInput && !targetInput.disabled) {
            targetInput.focus();
            targetInput.select();
            return true;
        }
        return false;
    }

    /**
     * 跳转到当前 MAC 的指定边界字节
     * @param {HTMLInputElement} currentInput - 当前输入框
     * @param {number} boundaryByteIndex - 边界字节索引：0 表示第一个，5 表示最后一个
     * @returns {boolean} 是否成功跳转
     */
    function navigateToMacBoundary(currentInput, boundaryByteIndex) {
        const context = getInputContext(currentInput);
        if (!context) return false;

        const targetInput = context.group.inputs[boundaryByteIndex];
        if (targetInput && !targetInput.disabled) {
            targetInput.focus();
            targetInput.select();
            return true;
        }
        return false;
    }

    /**
     * 创建 MAC 输入框组（6个小框）
     * @param {string} mac - MAC 地址字符串
     * @param {number} index - MAC 索引
     * @returns {HTMLElement}
     */
    function createMacInputGroup(mac, index) {
        const bytes = macToBytes(mac);
        const container = document.createElement("div");
        container.className = "mac-input-group";
        container.dataset.index = index;

        const inputs = [];

        for (let i = 0; i < 6; i++) {
            const input = document.createElement("input");
            input.type = "text";
            input.maxLength = 2;
            input.size = 2;
            input.className = "mac-byte-input";
            input.value = bytes[i];
            input.dataset.byteIndex = i;
            input.disabled = !macWritable;
            input.readOnly = true;

            // 输入验证和格式化
            input.addEventListener("input", (e) => {
                handleByteInput(e, input, inputs, i);
            });

            input.addEventListener("keydown", (e) => {
                handleByteKeydown(e, input, inputs, i);
            });

            input.addEventListener("focus", (e) => {
                input.select();
            });

            container.appendChild(input);

            // 添加分隔符（除了最后一个）
            if (i < 5) {
                const separator = document.createElement("span");
                separator.className = "mac-separator";
                separator.textContent = "-";
                container.appendChild(separator);
            }

            inputs.push(input);
        }

        // 存储输入框引用
        container.inputs = inputs;

        // 为容器添加验证状态显示
        const validationStatus = document.createElement("span");
        validationStatus.className = "mac-validation-status";
        container.appendChild(validationStatus);
        container.validationStatus = validationStatus;

        return container;
    }

    /**
     * 处理字节输入事件
     */
    function handleByteInput(e, currentInput, allInputs, byteIndex) {
        let value = e.target.value.toUpperCase();

        // 只允许十六进制字符
        value = value.replace(/[^0-9A-F]/g, "");

        // 限制长度为2
        if (value.length > 2) {
            value = value.slice(0, 2);
        }

        currentInput.value = value;

        if (value.length === 2 && byteIndex < 5) {
            // 如果输入了2个字符且不是最后一个字节，自动跳转到下一个字节
            navigateWithinMac(currentInput, allInputs, byteIndex, 1)
        } else if (value.length === 2 && byteIndex === 5) {
            // 如果输入了2个字符且是最后一个字节，自动跳转到下一个 MAC 的开头
            navigateToAdjacentMac(currentInput, 1, 0);
        }

        // 验证当前输入框的MAC组
        validateMacGroup(currentInput.closest(".mac-input-group"));
    }

    /**
     * 处理字节键盘事件
     */
    function handleByteKeydown(e, currentInput, allInputs, byteIndex) {
        // 处理删除键：如果当前输入框为空且按下了 Backspace，跳转到上一个输入框
        if (e.key === "Backspace" && currentInput.value === "" && byteIndex > 0) {
            e.preventDefault();
            navigateWithinMac(currentInput, allInputs, byteIndex, -1)
            return;
        }

        // 处理左箭头键
        if (e.key === "ArrowLeft") {
            e.preventDefault();
            // 尝试在当前 MAC 内向左移动
            if (navigateWithinMac(currentInput, allInputs, byteIndex, -1)) {
                return;
            }
            // 如果已在第一个字节，跳转到上一个 MAC 的最后一个字节
            navigateToAdjacentMac(currentInput, -1, 5);
            return;
        }

        // 处理右箭头键
        if (e.key === "ArrowRight") {
            e.preventDefault();
            // 尝试在当前 MAC 内向右移动
            if (navigateWithinMac(currentInput, allInputs, byteIndex, 1)) {
                return;
            }
            // 如果已在最后一个字节，跳转到下一个 MAC 的第一个字节
            navigateToAdjacentMac(currentInput, 1, 0);
            return;
        }

        // 处理上箭头键：切换到上一个 MAC 的相同字节
        if (e.key === "ArrowUp") {
            e.preventDefault();
            navigateToAdjacentMac(currentInput, -1, byteIndex);
            return;
        }

        // 处理下箭头键：切换到下一个 MAC 的相同字节
        if (e.key === "ArrowDown") {
            e.preventDefault();
            navigateToAdjacentMac(currentInput, 1, byteIndex);
            return;
        }

        // 处理 Home 键：跳转到当前 MAC 的第一个字节
        if (e.key === "Home") {
            e.preventDefault();
            navigateToMacBoundary(currentInput, 0);
            return;
        }

        // 处理 End 键：跳转到当前 MAC 的最后一个字节
        if (e.key === "End") {
            e.preventDefault();
            navigateToMacBoundary(currentInput, 5);
            return;
        }
    }

    /**
     * 验证单个 MAC 输入组
     * @param {HTMLElement} group - MAC 输入组容器
     * @returns {boolean}
     */
    function validateMacGroup(group) {
        if (!group || !group.inputs) return false;

        const bytes = group.inputs.map(input => input.value.padStart(2, ''));
        const isValid = macEnvBase ? isValidUnicastMac(bytes) : isValidMac(bytes);

        // 更新样式
        group.inputs.forEach(input => {
            const byteValue = input.value;
            const byteValid = isValidHexByte(byteValue);
            if (byteValid) {
                input.classList.remove("invalid");
                input.classList.add("valid");
            } else if (byteValue.length > 0) {
                input.classList.remove("valid");
                input.classList.add("invalid");
            } else {
                input.classList.remove("valid", "invalid");
            }
        });

        // 更新验证状态图标
        if (group.validationStatus) {
            if (isValid) {
                group.validationStatus.textContent = "✓";
                group.validationStatus.className = "mac-validation-status valid";
            } else {
                group.validationStatus.textContent = "✗";
                group.validationStatus.className = "mac-validation-status invalid";
            }
        }

        return isValid;
    }

    /**
     * 获取所有 MAC 地址
     * @returns {Array<string>}
     */
    function getAllMacs() {
        const groups = document.querySelectorAll(".mac-input-group");
        const macs = [];

        groups.forEach(group => {
            if (group.inputs) {
                const bytes = group.inputs.map(input => input.value.padStart(2, '0'));
                const mac = bytesToMac(bytes);
                macs.push(mac);
            }
        });

        return macs;
    }

    /**
     * 验证所有 MAC 地址
     * @returns {boolean}
     */
    function validateAllMacs() {
        const groups = document.querySelectorAll(".mac-input-group");
        let allValid = true;

        groups.forEach(group => {
            const isValid = validateMacGroup(group);
            if (!isValid) allValid = false;
        });

        return allValid;
    }

    /**
     * 渲染 MAC 地址表格
     * @param {Array<string>} macs - MAC 地址数组
     */
    function renderTable(macs) {
        const els = getElements();

        if (!els.tableBody) return;

        els.tableBody.innerHTML = "";

        if (!macs || macs.length === 0) {
            const row = document.createElement("tr");
            const cell = document.createElement("td");
            cell.colSpan = 2;
            cell.className = "mac-empty-cell";
            cell.setAttribute("data-i18n", "mac.no_data");
            cell.textContent = t("mac.no_data");
            row.appendChild(cell);
            els.tableBody.appendChild(row);
            return;
        }

        macs.forEach((mac, index) => {
            const row = document.createElement("tr");
            row.className = "mac-row";

            // 名称列
            const nameCell = document.createElement("td");
            nameCell.className = "mac-name-cell";
            nameCell.textContent = `MAC${index + 1}`;
            row.appendChild(nameCell);

            // 输入框列
            const inputCell = document.createElement("td");
            inputCell.className = "mac-input-cell";
            const inputGroup = createMacInputGroup(mac, index);
            inputCell.appendChild(inputGroup);
            row.appendChild(inputCell);

            els.tableBody.appendChild(row);
        });

        // 应用国际化
        applyI18n(els.tableBody);
    }

    /**
     * 启用编辑模式
     */
    function enableEditMode() {
        if (!macWritable) {
            setStatus(t("mac.status.read_only"), true);
            return;
        }
        const els = getElements();
        const inputs = document.querySelectorAll(".mac-byte-input");

        // 启用所有输入框
        inputs.forEach(input => {
            input.disabled = false;
            input.readOnly = false;
        });

        // 显示保存/取消按钮，隐藏修改按钮
        if (els.modifyBtn) els.modifyBtn.style.display = "none";
        if (els.saveCancelGroup) els.saveCancelGroup.style.display = "flex";

        setStatus(t("mac.status.edit_mode"));
    }

    /**
     * 禁用编辑模式（取消编辑）
     */
    function disableEditMode() {
        const els = getElements();

        // 重新加载原始数据
        loadMacInfo();

        // 显示修改按钮，隐藏保存/取消按钮
        if (els.modifyBtn) els.modifyBtn.style.display = "inline-block";
        if (els.saveCancelGroup) els.saveCancelGroup.style.display = "none";

        setStatus(t("mac.status.cancelled"));
    }

    /**
     * 保存 MAC 地址
     */
    async function saveMacs() {
        // 验证所有 MAC 地址
        if (!validateAllMacs()) {
            setStatus(t("mac.error.invalid_mac"), true);
            return;
        }

        const macs = getAllMacs();
        if (macEnvBase && (!macWritable || macs.length !== 1 ||
                !isValidUnicastMac(macToBytes(macs[0])))) {
            setStatus(t("mac.error.invalid_mac"), true);
            return;
        }

        if (!macs || macs.length === 0) {
            setStatus(t("mac.error.no_mac"), true);
            return;
        }

        // 将 MAC 地址数组转换为分号分隔的字符串
        const macsString = macs.join(';');

        try {
            setStatus(t("mac.status.saving"));

            const formData = new FormData();
            formData.append("mac_data", macsString);

            const response = await fetch("/mac/set", {
                method: "POST",
                body: formData
            });

            const result = await response.text();

            if (!response.ok) {
                throw new Error(`${t("mac.status.http")} ${response.status}: ${result}`);
            }

            if (result === "ok") {
                if (macEnvBase) {
                    const refreshed = await loadMacInfo();
                    if (!refreshed || !Array.isArray(refreshed.macs) || refreshed.macs.length !== 1 ||
                            refreshed.macs[0].toUpperCase() !== macs[0].toUpperCase())
                        throw new Error(t("mac.status.verify_failed"));
                }
                const els = getElements();
                document.querySelectorAll(".mac-byte-input").forEach(input => {
                    input.disabled = true;
                    input.readOnly = true;
                });
                if (els.modifyBtn) els.modifyBtn.style.display = "inline-block";
                if (els.saveCancelGroup) els.saveCancelGroup.style.display = "none";
                setStatus(t("mac.status.saved"));
            } else {
                throw new Error(result || t("mac.status.error"));
            }

        } catch (error) {
            setStatus(formatError(error), true);
        }
    }

    /**
     * 取消之前的延迟刷新定时器
     */
    function cancelDelayedRefresh() {
        if (refreshTimer) {
            clearTimeout(refreshTimer);
            refreshTimer = null;
        }
    }

    /**
     * 延迟刷新 MAC 列表
     * @param {number} delay - 延迟时间（毫秒），默认 1500ms
     */
    function delayedRefresh(delay) {
        cancelDelayedRefresh();

        delay = delay || 1500;

        refreshTimer = setTimeout(async () => {
            refreshTimer = null;
            await loadMacInfo();
        }, delay);
    }

    /**
     * 加载 MAC 地址信息
     */
    async function loadMacInfo() {
        try {
            setStatus(t("mac.status.loading"));

            const response = await fetch("/mac/info", {
                method: "GET",
                cache: "no-store",
                headers: {
                    "Accept": "application/json"
                }
            });

            if (!response.ok) {
                throw new Error(`${t("mac.status.http")} ${response.status}`);
            }

            const data = await response.json();
            setMacStoragePresentation(data);

            // 解析分区名称
            if (data && data.part_name) {
                partName = data.part_name;
                setPartName(partName);
            } else {
                partName = "";
                setPartName("");
            }

            // 解析 MAC 列表
            if (data && data.macs && Array.isArray(data.macs)) {
                macList = data.macs;
                renderTable(macList);
                setStatus(t("mac.status.ready"));
                return data;
            } else {
                throw new Error(t("mac.error.invalid_response"));
            }

        } catch (error) {
            setStatus(formatError(error), true);
            setPartName("");
            renderTable([]);
            setMacStoragePresentation(null);
            return null;
        }
    }

    /**
     * 绑定事件
     */
    function bindEvents() {
        const els = getElements();

        if (els.modifyBtn) {
            els.modifyBtn.addEventListener("click", enableEditMode);
        }

        if (els.saveBtn) {
            els.saveBtn.addEventListener("click", saveMacs);
        }

        if (els.cancelBtn) {
            els.cancelBtn.addEventListener("click", disableEditMode);
        }
    }

    /**
     * 初始化 MAC 管理器
     */
    async function init() {
        getElements();
        if (elements && elements.modifyBtn) elements.modifyBtn.disabled = true;
        bindEvents();
        loadMacInfo();
    }

    return {
        init,
        refresh: loadMacInfo,
        save: saveMacs
    };
})();

// ==============================
// 国际化数据
// ==============================

/**
 * 以下 key 和对应的 value 来自项目 Yuzhii0718/bl-mt798x-dhcpd:
 * common.update, common.warnings, backup.hint, backup.label.mode, backup.label.target,
 * backup.target.placeholder, backup.warn.1, backup.warn.2, backup.warn.3, env.title,
 * env.hint, env.warn.1, env.warn.2, reboot.confirm, reboot.title.in_progress,
 * initramfs.title (en), initramfs.hint (en), initramfs.boot_hint, initramfs.warn.1 (en),
 * flashing.title.in_progress, flashing.title.done,
 * booting.title.in_progress (en), booting.title.done, booting.hint.done (en), 404.title, 404.hint
 *
 * 以下 key 和对应的 value 修改自项目 Yuzhii0718/bl-mt798x-dhcpd:
 * flashing.hint.in_progress
 *
 * 以下模板函数内容修改自项目 Yuzhii0718/bl-mt798x-dhcpd:
 * updateHint, warnChoose， warnDanger (en)
 */
const I18N = (() => {
    const t = {
        en: {
            updateHint: (type) => `You are going to update <strong>${type}</strong> on the device.<br>Please choose a file from your local hard drive and click <strong>Upload</strong> button.`,
            warnChoose: (type) => `You can upload whatever you want, so be sure you choose the proper ${type} for your device.`,
            warnDanger: (type) => `Updating ${type} is a very dangerous operation and may damage your device.`
        },
        "zh-cn": {
            updateHint: (type) => `你将要在此设备上更新 <strong>${type}</strong>。<br>请选择本地文件并点击 <strong>上传</strong> 按钮。`,
            warnChoose: (type) => `你可以上传任意文件，请确保选择了与你的设备匹配的 ${type} 文件。`,
            warnDanger: (type) => `更新 ${type} 是一个十分危险的操作，可能导致你的路由器无法启动。`
        }
    };

    return {
        en: {
            "app.name": "uBootKit",
            "nav.overview": "Overview",
            "nav.sysinfo": "System Info",
            "nav.syslog": "System Log",
            "nav.update": "Update",
            "nav.firmware": "Firmware Update",
            "nav.art": "ART Update",
            "nav.cdt": "CDT Update",
            "nav.ptable": "PTABLE Update",
            "nav.simg": "SIMG Update",
            "nav.uboot": "U-Boot Update",
            "nav.debrick": "Debrick",
            "nav.mibib": "MIBIB Reload",
            "nav.initramfs": "Load Initramfs",
            "nav.system": "System",
            "nav.settings": "Theme Settings",
            "nav.network": "Network Settings",
            "nav.backup": "Flash Backup",
            "nav.webterm": "Web Terminal",
            "nav.mac": "MAC Manage",
            "nav.env": "Env Manage",
            "nav.reboot": "Reboot",
            "title.author": "View Author Profile",
            "title.project": "View Project",
            "file.dropzone.text": "Drag & drop a file here or click to select a file",
            "file.dropzone.hint": "Please select the appropriate file",
            "file.uploading": "Uploading",
            "file.processing": "Processing",
            "common.upload": "Upload",
            "common.update": "Update",
            "common.update_reboot": "Update & Reboot",
            "common.boot": "Boot",
            "common.upgrade_hint": 'If all information above is correct, click "Update" or "Update & Reboot".',
            "common.warnings": "WARNINGS",
            "common.warn.1": "Do not power off the device during update.",
            "file.select": "Please select a file first!",
            "label.name": "File",
            "label.type": "Type",
            "label.size": "Size",
            "label.md5": "MD5",
            "label.target": "Target",
            "label.second_target": "Second target",
            "firmware.title": "FIRMWARE UPDATE",
            "firmware.hint": t.en.updateHint("firmware"),
            "firmware.warn.1": t.en.warnChoose("firmware image"),
            "uboot.title": "U-BOOT UPDATE",
            "uboot.hint": t.en.updateHint("U-Boot (bootloader)"),
            "uboot.warn.1": t.en.warnChoose("U-Boot image"),
            "uboot.warn.2": t.en.warnDanger("U-Boot"),
            "art.title": "ART UPDATE",
            "art.hint": t.en.updateHint("ART (Atheros Radio Test)"),
            "art.warn.1": "ART has no fixed magic number, so its file type will be recognized as Unknown.",
            "art.warn.2": t.en.warnChoose("ART image"),
            "cdt.title": "CDT UPDATE",
            "cdt.hint": t.en.updateHint("CDT (Configuration Data Table)"),
            "cdt.warn.1": t.en.warnChoose("CDT image"),
            "cdt.warn.2": t.en.warnDanger("CDT"),
            "ptable.title": "PTABLE UPDATE",
            "ptable.hint": t.en.updateHint("Partition Table (GPT or MIBIB)"),
            "ptable.warn.1": t.en.warnChoose("PTABLE"),
            "ptable.warn.2": t.en.warnDanger("PTABLE"),
            "simg.title": "SIMG UPDATE",
            "simg.hint": t.en.updateHint("Single Image (written starting from offset 0x0 of the flash memory device)"),
            "simg.warn.1": t.en.warnChoose("Single Image"),
            "simg.warn.2": t.en.warnDanger("Single Image"),
            "backup.title": "FLASH BACKUP",
            "backup.hint": "Download a backup from device storage as a <strong>binary file</strong>.<br>The backup data will be streamed to your browser and saved on your computer.",
            "backup.label.mode": "Mode:",
            "backup.label.target": "Target:",
            "backup.label.item": "Item:",
            "backup.label.start": "Start:",
            "backup.label.size": "Size:",
            "backup.mode.full": "Full backup",
            "backup.mode.range": "Custom range backup",
            "backup.target.placeholder": "-- Select target --",
            "backup.target.raw": "RAW Flash Devices",
            "backup.target.smem": "SMEM Partitions",
            "backup.target.mmc": "MMC Partitions",
            "backup.range.hint": "Start offset and size (supports hex 0x... or decimal with K/KiB suffix)",
            "backup.range.error.empty_start": "No start offset",
            "backup.range.error.empty_size": "No size",
            "backup.range.error.invalid_start": "Invalid start address format (e.g. 0x1000 or 4096 or 4k)",
            "backup.range.error.invalid_size": "Invalid size format (e.g. 0x1000 or 64k)",
            "backup.range.error.size_zero": "Size must be greater than 0",
            "backup.status.pending": "⏳ Pending",
            "backup.status.downloading": "⬇️ Downloading",
            "backup.status.done": "✅ Done",
            "backup.status.error": "❌ Error",
            "backup.items.count": "Items",
            "backup.items.empty": "No items available",
            "backup.badge.large": "Large",
            "backup.progress.title": "Download Progress",
            "backup.confirm.download": "About to download, continue?\n\nNumber of items to be downloaded: ",
            "backup.action.select_all": "Select All (excl. large)",
            "backup.action.select_all_force": "Select All (incl. large)",
            "backup.action.deselect_all": "Deselect All",
            "backup.action.download": "Download",
            "backup.error.invalid_input": "Invalid input",
            "backup.error.no_selection": "Please select at least one item",
            "backup.error.downloading": "Download in progress, please wait...",
            "backup.error.range_no_selection": "Please select an item",
            "backup.error.no_target": "Please select a target",
            "backup.error.bad_range": "Invalid range",
            "backup.error.exception": "Error:",
            "backup.warn.1": "Do not power off the device during backup.",
            "backup.warn.2": "Custom range reads raw bytes; be careful with offsets.",
            "backup.warn.3": "Large backups may take a long time depending on storage speed.",
            "backup.warn.4": "Entries exceeding 256 MiB will be marked as large files, which are not selected by default.",
            "backup.warn.5": "When downloading multiple files in batches, it is recommended to enable the auto-save function in your browser's download settings.",
            "webterm.title": "WEB TERMINAL",
            "webterm.input": "Input",
            "webterm.send": "Send",
            "webterm.clear": "Clear",
            "webterm.cmd.forbid.hint": "this command has been disabled!",
            "webterm.cmd.forbid.reason.common": "This command will cause the HTTPD service to exit abnormally. Do not use it in the web terminal!",
            "webterm.cmd.nomatch": "no matching command",
            "webterm.cmd.placeholder": "help; printenv",
            "webterm.cmd.unknown": "unknown command!",
            "webterm.status.ready": "Ready",
            "webterm.status.running": "Running...",
            "webterm.status.done": "Done",
            "webterm.status.cleared": "Cleared",
            "webterm.status.http": "HTTP error:",
            "webterm.status.parse": "Parse error",
            "webterm.status.error": "Error:",
            "webterm.upload": "Upload",
            "webterm.uploading": "Uploading:",
            "webterm.upload.success": "Upload successful",
            "env.title": "U-BOOT ENV",
            "env.hint": "Manage <strong>U-Boot environment variables</strong>. Changes will be saved to storage.",
            "env.mode.readonly": "Read-only view of the current RAM environment for this boot session. Changes are not saved to persistent storage.",
            "env.mode.protected": "Persistent ENV editing is enabled for this board. bootcmd, bootargs, bootdelay, ethact, mtdparts and mtdids remain protected to preserve the current boot strategy and partition mapping.",
            "env.count": "Variables:",
            "env.status.loading": "Loading...",
            "env.status.ready": "Ready",
            "env.status.saving": "Saving...",
            "env.status.saved": "✓ Saved",
            "env.status.deleted": "✓ Deleted",
            "env.status.reset": "✓ Reset to defaults",
            "env.status.reset_single": "✓ Variable reset to default",
            "env.status.restored": "✓ Restored",
            "env.status.http": "HTTP error:",
            "env.status.error": "Error",
            "env.thead.name": "Name",
            "env.thead.value": "Value",
            "env.thead.actions": "Actions",
            "env.action.refresh": "Refresh",
            "env.action.reset": "Reset to defaults",
            "env.action.restore": "Restore from file",
            "env.action.add": "Add",
            "env.action.edit": "Edit",
            "env.action.save": "Save",
            "env.action.delete": "Delete",
            "env.action.cancel": "Cancel",
            "env.action.reset_single": "Reset to Default",
            "env.delete.forbid": "the variable cannot be deleted!",
            "env.confirm.reset_single": "reset this variable to default value?",
            "env.confirm.delete": "Delete variable",
            "env.confirm.reset": "Reset all environment variables to defaults?",
            "env.confirm.restore": "Restore environment from file? This will overwrite all current variables.",
            "env.error.no_name": "Please enter variable name",
            "env.error.no_file": "Please select a file",
            "env.placeholder": "No environment variables loaded",
            "env.placeholder.name": "Enter variable name",
            "env.placeholder.value": "Enter variable value",
            "env.error.already_exists": "Variable already exists!",
            "env.status.adding": "Adding new variable...",
            "env.status.edit_mode": "Editing: ",
            "env.warn.1": "Modifying environment variables may affect boot behavior.",
            "env.warn.2": "Do not power off during save or restore.",
            "env.warn.3": "The RESTORE function supports uploading a previously saved binary U-Boot environment image (CRC + data) to restore.",
            "mac.title": "MAC ADDRESS MANAGEMENT",
            "mac.hint": "View and modify device <strong>MAC addresses</strong>.",
            "mac.no_data": "No MAC address data available",
            "mac.label.part_name": "MAC Address Partition:",
            "mac.status.loading": "Loading MAC addresses...",
            "mac.status.ready": "Ready",
            "mac.status.saving": "Saving...",
            "mac.status.saved": "✓ MAC addresses saved",
            "mac.status.edit_mode": "Edit mode - modify MAC addresses and click Save",
            "mac.status.cancelled": "Edit cancelled",
            "mac.status.http": "HTTP error:",
            "mac.status.error": "Error",
            "mac.error.invalid_mac": "Invalid MAC address format",
            "mac.error.no_mac": "No MAC address data to save",
            "mac.error.invalid_response": "Invalid server response",
            "mac.action.modify": "Modify",
            "mac.action.save": "Save",
            "mac.action.cancel": "Cancel",
            "mac.warn.1": "Backup MAC address partition before making any changes.",
            "mac.warn.2": "These are just the raw MAC data in the partition; whether and how they are used is determined by the firmware.",
            "mac.warn.3": "After modification, the firmware needs to be reset or reflashed for the modified MAC address to be recognized.",
            "mac.warn.4": "Invalid MAC addresses may cause network issues.",
            "mac.warn.uboot.1": "This changes only U-Boot ethaddr in APPSBLENV; raw ART data is unchanged.",
            "mac.warn.uboot.2": "Other MAC fields stay unchanged. This does not establish which addresses OEM firmware uses.",
            "mac.warn.uboot.3": "APPSBLENV is read back before success; running interfaces may need a restart.",
            "mac.warn.uboot.4": "Use one valid unicast base address. Invalid MAC addresses may cause network issues.",
            "mac.status.read_only": "Persistent MAC editing is unavailable.",
            "mac.status.verify_failed": "The saved U-Boot base MAC could not be confirmed by readback.",
            "mibib.title": "MIBIB RELOAD",
            "mibib.hint": "In <strong>9008</strong> mode, reloading <strong>MIBIB</strong> to initialize <strong>SMEM (Shared Memory) Partition Info</strong>. <br>Please choose a file from your local hard drive and click <strong>Reload</strong> button.",
            "mibib.reload": "Reload",
            "mibib.reload_success_hint": "MIBIB reloaded successfully. Please click the button below to go to the \"System Info\" page to check if the SMEM information is correct.",
            "mibib.reload_success": "View system info",
            "mibib.not_9008": "Not in 9008 mode, MIBIB reload is not allowed!",
            "mibib.warn.1": "Use only in 9008 emergency download mode.",
            "mibib.warn.2": "After successful reload, please check if the SMEM information is correct.",
            "network.title": "NETWORK SETTINGS",
            "network.hint": "Configure <strong>U-Boot network parameters</strong> (ipaddr, netmask, serverip).<br>Changes will be saved to environment variables and will take effect after a restart.",
            "network.label.ipaddr": "IP Address:",
            "network.label.netmask": "Netmask:",
            "network.label.serverip": "Server IP:",
            "network.action.refresh": "Refresh",
            "network.action.save": "Save",
            "network.action.reset": "Reset to defaults",
            "network.status.loading": "Loading network settings...",
            "network.status.ready": "Ready",
            "network.status.saving": "Saving...",
            "network.status.saved": "✓ Network settings saved",
            "network.status.reset": "✓ Reset to defaults",
            "network.status.http": "HTTP error:",
            "network.status.error": "Error",
            "network.confirm.reset": "Reset all network settings to defaults?",
            "network.validation.valid": "Valid",
            "network.validation.invalid_ip": "Invalid IP address format",
            "network.validation.invalid_netmask": "Invalid netmask format",
            "network.validation.cannot_be_empty": "cannot be empty",
            "network.validation.invalid_format": "has invalid format",
            "network.validation.invalid_netmask_format": "has invalid netmask format",
            "network.warn.1": "Modifying network settings may affect network connectivity.",
            "network.warn.2": "Do not power off the device during save.",
            "network.warn.3": "Invalid network settings may prevent network access.",
            "settings.title": "SETTINGS",
            "settings.hint": "Configure <strong>application preferences</strong> including language and theme.",
            "settings.label.language": "Language",
            "settings.label.theme": "Theme",
            "settings.theme.auto": "Auto",
            "settings.theme.light": "Light",
            "settings.theme.dark": "Dark",
            "settings.warn.1": "Language and theme settings are saved in your browser's local storage.",
            "settings.warn.2": "Theme changes apply immediately. Auto mode follows your system preference.",
            "sysinfo.title": "SYSTEM INFORMATION",
            "sysinfo.title.board_info": "Board Info",
            "sysinfo.title.flash_info": "Flash Info",
            "sysinfo.title.partitions_info": "Partitions",
            "sysinfo.loading": "Loading system information...",
            "sysinfo.error.load": "Failed to load system information",
            "sysinfo.error.parse": "Failed to parse system information",
            "sysinfo.uboot_version": "U-Boot Version",
            "sysinfo.hostname": "Hostname",
            "sysinfo.build_name": "Build Configuration Name",
            "sysinfo.family": "Hardware Family",
            "sysinfo.model": "Model",
            "sysinfo.compatible": "Compatible",
            "sysinfo.machid": "Machine ID",
            "sysinfo.ram_size": "RAM Size",
            "sysinfo.smeminfo": "SMEM INFO",
            "sysinfo.flash_type": "Flash Type",
            "sysinfo.flash_block_size": "Block Size",
            "sysinfo.flash_density": "Density",
            "sysinfo.bus": "Bus",
            "sysinfo.chip_select": "Chip Select",
            "sysinfo.present": "Present",
            "sysinfo.name": "Name",
            "sysinfo.size": "Size",
            "sysinfo.page_size": "Page Size",
            "sysinfo.sector_size": "Sector Size",
            "sysinfo.erase_size": "Erase Size",
            "sysinfo.block_size": "Block Size",
            "sysinfo.oob_size": "OOB Size",
            "sysinfo.oob_avail": "OOB Available",
            "sysinfo.ecc_step_size": "ECC Step Size",
            "sysinfo.ecc_strength": "ECC Strength",
            "sysinfo.vendor": "Vendor",
            "sysinfo.product": "Product",
            "sysinfo.version": "Version",
            "sysinfo.emmc_life": "MMC LIFE INFO",
            "sysinfo.status.normal": "Normal",
            "sysinfo.status.warning": "Warning",
            "sysinfo.status.urgent": "Urgent",
            "sysinfo.status.unknown": "Unknown",
            "sysinfo.type": "Type",
            "sysinfo.part_index": "Index",
            "sysinfo.part_start": "Start Address",
            "sysinfo.part_end": "End Address",
            "sysinfo.no_data": "No data available",
            "sysinfo.boot_mode": "Boot Mode",
            "syslog.title": "SYSTEM LOG",
            "syslog.copy": "Copy",
            "syslog.clear": "Clear",
            "syslog.save": "Save",
            "syslog.auto_scroll": "Auto Scroll",
            "syslog.confirm.clear": "Clear all logs?",
            "syslog.status.copied": "Copied to clipboard",
            "syslog.status.copy_failed": "Failed to copy to clipboard",
            "syslog.status.ready": "Ready",
            "syslog.status.cleared": "Cleared",
            "syslog.status.saved": "Saved",
            "syslog.status.no_content": "No log content",
            "syslog.status.auto_scroll_on": "Auto-scroll enabled",
            "syslog.status.auto_scroll_off": "Auto-scroll disabled",
            "syslog.status.http": "HTTP error:",
            "syslog.warn.1": "Logs are fetched via polling and updated automatically.",
            "syslog.warn.2": "Logs are stored in browser session storage and will be cleared when the page is closed.",
            "reboot.confirm": "Reboot device now?",
            "reboot.title.in_progress": "REBOOTING DEVICE",
            "reboot.hint.in_progress": "Reboot request has been sent. Please wait...<br>This page may be in not responding status for a short time.",
            "initramfs.title": "LOAD INITRAMFS",
            "initramfs.hint": "You are going to load <strong>Initramfs<\/strong> on the device.<br>Please choose a file from your local hard drive and click <strong>Upload<\/strong> button.",
            "initramfs.boot_hint": 'If all information above is correct, click "Boot".',
            "initramfs.warn.1": "If everything goes well, the device will boot into the Initramfs.",
            "initramfs.warn.2": t.en.warnChoose("Initramfs image"),
            "flashing.title.in_progress": "UPDATE IN PROGRESS",
            "flashing.hint.in_progress": "File uploaded successfully! Update is in progress, please wait patiently.<br>Update time depends on image size and may take up to a few minutes.",
            "flashing.title.done": "UPDATE COMPLETED",
            "flashing.hint.done": "Your device was successfully updated! Now rebooting...",
            "flashing.hint.continue": "Your device was successfully updated!",
            "flashing.msg.continue": "You are currently still in U-Boot Web recovery mode. Please continue with other operations or manually restart your device.",
            "booting.title.in_progress": "BOOTING INITRAMFS",
            "booting.hint.in_progress": "Your file was successfully uploaded! Booting is in progress, please wait...<br>This page may be in not responding status for a short time.",
            "booting.title.done": "BOOT SUCCESS",
            "booting.hint.done": "Your device was successfully booted into initramfs!",
            "404.title": "PAGE NOT FOUND",
            "404.hint": "The page you were looking for doesn't exist!",
            "fail.title.validate": "VALIDATION FAILED",
            "fail.title.update": "UPDATE FAILED",
            "fail.hint.validate": "The file failed validation.",
            "fail.hint.update": "Something went wrong during update.",
            "error.title": "Error",
            "metadata.error.stale_commit": "The one-use token is stale or does not match this metadata upload. Upload and validate the file again.",
            "metadata.error.staged_invalid": "The staged image no longer passes validation. Upload and validate the file again.",
            "metadata.error.bytes_changed": "Staged bytes differ from the validated upload. Do not retry without validating again.",
            "metadata.error.layout_changed": "The validated partition layout changed. Read the layout and validate again.",
            "metadata.error.write_failed": "Metadata write or readback verification failed. Inspect the device before another attempt.",
            "metadata.error.staging_failed": "The owned staging area could not be validated. Inspect the device before another attempt.",
            "metadata.error.uboot_partial": "A U-Boot copy could not be written or verified. Download both original backups before power-off. Further U-Boot updates are locked until restart.",
            "metadata.error.uboot_locked": "A previous U-Boot update is partial. Download the original backups and inspect both copies before restart.",
            "metadata.error.single_needs_slot1": "The big partition boots only from physical slot 1 (0x900000). Set it as the default boot slot first, then validate again.",
            "metadata.error.uboot_preflight": "U-Boot preflight failed. Neither copy was erased. Check the partition layout, NAND and available memory.",
            "metadata.error.uboot_failed": "U-Boot update stopped before erase. Inspect the device and validate the image again.",
            "metadata.backup.primary": "Download original APPSBL",
            "metadata.backup.secondary": "Download original APPSBL_1",
            "metadata.error.staging_unavailable": "The metadata staging area is unavailable. Check available memory and validate the file again.",
            "error.label.file_type": "File Type",
            "error.label.file_size": "File Size",
            "error.label.part_name": "Partition Name",
            "error.label.part_size": "Partition Size",
            "error.label.expected_type": "Expected Type",
            "error.label.actual_type": "Actual Type",
            "error.label.flash_type": "Flash Type",
            "error.label.filename": "Filename",
            "error.label.cmd": "Command",
            "error.label.cmd_output": "Command Output",
            "error.no_output": "No output",
            "error.unit.bytes": "Bytes",
            "error.file_too_big": "File Too Large",
            "error.part_not_found": "Partition Not Found",
            "error.wrong_file_type": "Wrong File Type",
            "error.flash_not_found": "Flash Not Found",
            "error.run_cmd_failed": "Command Execution Failed",
            "error.invalid_response": "Invalid Server Response",
            "error.suggestion.file_too_big": "Please choose a file smaller than the partition size or expand the partition.",
            "error.suggestion.part_not_found": "The target partition does not exist or is not available.<br>Please check your device partition table or contact support.",
            "error.suggestion.wrong_file_type": "Please choose the correct file type.",
            "error.suggestion.flash_not_found": "No flash device can be found that corresponds to the selected file type.",
            "unknown": "Unknown"
        },
        "zh-cn": {
            "app.name": "uBootKit",
            "nav.overview": "概览",
            "nav.sysinfo": "系统信息",
            "nav.syslog": "系统日志",
            "nav.update": "更新",
            "nav.firmware": "固件更新",
            "nav.art": "ART 更新",
            "nav.cdt": "CDT 更新",
            "nav.ptable": "分区表更新",
            "nav.simg": "闪存镜像更新",
            "nav.uboot": "U-Boot 更新",
            "nav.debrick": "救砖",
            "nav.mibib": "MIBIB 重载",
            "nav.initramfs": "Initramfs 启动",
            "nav.system": "系统",
            "nav.settings": "主题设置",
            "nav.network": "网络设置",
            "nav.backup": "闪存备份",
            "nav.webterm": "网页终端",
            "nav.mac": "MAC 管理",
            "nav.env": "环境变量",
            "nav.reboot": "重启",
            "title.author": "查看作者主页",
            "title.project": "查看项目",
            "file.dropzone.text": "将文件拖拽到此处，或点击以选择文件",
            "file.dropzone.hint": "请选择合适的文件",
            "file.uploading": "上传中",
            "file.processing": "处理中",
            "common.upload": "上传",
            "common.update": "更新",
            "common.update_reboot": "更新并重启",
            "common.boot": "启动",
            "common.upgrade_hint": "如果以上信息确认无误，请点击 “更新” 或 “更新并重启”。",
            "common.warnings": "注意事项",
            "common.warn.1": "刷写过程中请勿断电。",
            "file.select": "请选择文件！",
            "label.name": "文件",
            "label.type": "类型",
            "label.size": "大小",
            "label.md5": "MD5",
            "label.target": "\u76ee\u6807\u5206\u533a",
            "label.second_target": "\u7b2c\u4e8c\u76ee\u6807\u5206\u533a",
            "firmware.title": "固件更新",
            "firmware.hint": t["zh-cn"].updateHint("固件"),
            "firmware.warn.1": t["zh-cn"].warnChoose("固件"),
            "uboot.title": "U-BOOT 更新",
            "uboot.hint": t["zh-cn"].updateHint("U-Boot（引导程序）"),
            "uboot.warn.1": t["zh-cn"].warnChoose("U-Boot"),
            "uboot.warn.2": t["zh-cn"].warnDanger("U-Boot "),
            "art.title": "ART 更新",
            "art.hint": t["zh-cn"].updateHint("无线芯片频率校准数据 ART (Atheros Radio Test)"),
            "art.warn.1": "ART 无固定魔数，故其文件类型会被识别为 Unknown。",
            "art.warn.2": t["zh-cn"].warnChoose("ART"),
            "cdt.title": "CDT 更新",
            "cdt.hint": t["zh-cn"].updateHint("CDT (Configuration Data Table)"),
            "cdt.warn.1": t["zh-cn"].warnChoose("CDT"),
            "cdt.warn.2": t["zh-cn"].warnDanger("CDT "),
            "ptable.title": "分区表更新",
            "ptable.hint": t["zh-cn"].updateHint("分区表（GPT 或 MIBIB）"),
            "ptable.warn.1": t["zh-cn"].warnChoose("分区表"),
            "ptable.warn.2": t["zh-cn"].warnDanger("分区表"),
            "simg.title": "闪存镜像更新",
            "simg.hint": t["zh-cn"].updateHint("闪存镜像（从闪存设备的偏移量 0x0 处开始写入）"),
            "simg.warn.1": t["zh-cn"].warnChoose("闪存镜像"),
            "simg.warn.2": t["zh-cn"].warnDanger("闪存镜像"),
            "backup.title": "闪存备份",
            "backup.hint": "从设备存储下载备份为 <strong>二进制文件</strong>。<br>备份数据将流式传输到浏览器并保存到您的计算机。",
            "backup.label.mode": "模式:",
            "backup.label.target": "目标:",
            "backup.label.item": "条目:",
            "backup.label.start": "起始偏移:",
            "backup.label.size": "大小:",
            "backup.mode.full": "完整备份",
            "backup.mode.range": "自定义范围备份",
            "backup.target.placeholder": "-- 选择目标 --",
            "backup.target.raw": "RAW 闪存设备",
            "backup.target.smem": "SMEM 分区",
            "backup.target.mmc": "MMC 分区",
            "backup.range.hint": "起始偏移和大小支持十进制、0x 十六进制及 KiB / MiB 后缀",
            "backup.range.error.empty_start": "未输入起始偏移",
            "backup.range.error.empty_size": "未输入大小",
            "backup.range.error.invalid_start": "起始地址格式无效（正确示例：0x1000 或 4096 或 4k）",
            "backup.range.error.invalid_size": "大小格式无效（正确示例：0x1000 或 64k）",
            "backup.range.error.size_zero": "大小必须大于 0",
            "backup.status.pending": "⏳ 等待中",
            "backup.status.downloading": "⬇️ 下载中",
            "backup.status.done": "✅ 已完成",
            "backup.status.error": "❌ 错误",
            "backup.items.count": "条目数",
            "backup.items.empty": "暂无可用条目",
            "backup.badge.large": "大文件",
            "backup.progress.title": "下载进度",
            "backup.confirm.download": "即将开始下载，是否继续？\n\n待下载条目数：",
            "backup.action.select_all": "全选（不含大文件）",
            "backup.action.select_all_force": "全选（包含大文件）",
            "backup.action.deselect_all": "取消全选",
            "backup.action.download": "下载",
            "backup.error.invalid_input": "输入无效",
            "backup.error.no_selection": "请至少选择一个条目",
            "backup.error.downloading": "正在下载中，请稍候...",
            "backup.error.range_no_selection": "请选择一个条目",
            "backup.error.no_target": "请选择目标",
            "backup.error.bad_range": "无效的范围",
            "backup.error.exception": "错误:",
            "backup.warn.1": "备份过程中请勿断电。",
            "backup.warn.2": "自定义范围读取原始字节，请谨慎设置偏移量。",
            "backup.warn.3": "大容量备份可能需要较长时间，取决于存储速度。",
            "backup.warn.4": "超过 256 MiB 的条目会被标记为大文件，默认不会勾选。",
            "backup.warn.5": "批量下载多个文件时，建议在浏览器的下载设置中打开自动保存功能。",
            "webterm.title": "网页终端",
            "webterm.input": "输入",
            "webterm.send": "发送",
            "webterm.clear": "清空",
            "webterm.cmd.forbid.hint": "此命令已被禁止执行！",
            "webterm.cmd.forbid.reason.common": "此命令会导致 HTTPD 服务异常退出，请勿在网页终端中使用！",
            "webterm.cmd.nomatch": "没有匹配的命令",
            "webterm.cmd.placeholder": "help; printenv",
            "webterm.cmd.unknown": "未知命令！",
            "webterm.status.ready": "就绪",
            "webterm.status.running": "执行中...",
            "webterm.status.done": "完成",
            "webterm.status.cleared": "已清空",
            "webterm.status.http": "HTTP 错误：",
            "webterm.status.parse": "解析失败",
            "webterm.status.error": "错误：",
            "webterm.upload": "上传",
            "webterm.uploading": "正在上传：",
            "webterm.upload.success": "上传成功",
            "env.title": "U-BOOT 环境变量",
            "env.hint": "管理 <strong>U-Boot 环境变量</strong>。更改将保存到存储设备。",
            "env.mode.readonly": "此页面只读显示本次启动的当前 RAM 环境；更改不会保存到持久存储。",
            "env.mode.protected": "支持持久化 ENV 修改；为保护当前启动策略和分区映射，bootcmd、bootargs、bootdelay、ethact、mtdparts、mtdids 仍不可修改。",
            "env.count": "变量数:",
            "env.status.loading": "加载中...",
            "env.status.ready": "就绪",
            "env.status.saving": "保存中...",
            "env.status.saved": "✓ 已保存",
            "env.status.deleted": "✓ 已删除",
            "env.status.reset": "✓ 已重置为默认值",
            "env.status.restored": "✓ 已恢复",
            "env.status.http": "HTTP 错误:",
            "env.status.error": "错误",
            "env.status.reset_single": "✓ 变量已重置为默认值",
            "env.thead.name": "名称",
            "env.thead.value": "值",
            "env.thead.actions": "操作",
            "env.action.refresh": "刷新",
            "env.action.reset": "重置为默认值",
            "env.action.restore": "从文件恢复",
            "env.action.add": "添加",
            "env.action.edit": "编辑",
            "env.action.save": "保存",
            "env.action.delete": "删除",
            "env.action.cancel": "取消",
            "env.action.reset_single": "重置为默认值",
            "env.delete.forbid": "该变量不能删除！",
            "env.confirm.reset_single": "确定将此变量重置为默认值？",
            "env.confirm.delete": "删除变量",
            "env.confirm.reset": "确定将所有环境变量重置为默认值？",
            "env.confirm.restore": "确定从文件恢复环境变量？这将覆盖所有当前变量。",
            "env.error.no_name": "请输入变量名称",
            "env.error.no_file": "请选择文件",
            "env.placeholder": "暂无环境变量",
            "env.placeholder.name": "输入变量名",
            "env.placeholder.value": "输入变量值",
            "env.error.already_exists": "变量已存在！",
            "env.status.adding": "正在添加新变量...",
            "env.status.edit_mode": "正在编辑：",
            "env.warn.1": "修改环境变量可能影响系统启动行为。",
            "env.warn.2": "保存或恢复过程中请勿断电。",
            "env.warn.3": "恢复功能支持上传你之前保存的二进制环境变量镜像文件（含 CRC）进行恢复。",
            "mac.title": "MAC 地址管理",
            "mac.hint": "查看和修改设备的 <strong>MAC 地址</strong>。",
            "mac.no_data": "暂无 MAC 地址数据",
            "mac.label.part_name": "MAC 地址所在分区：",
            "mac.status.loading": "正在加载 MAC 地址...",
            "mac.status.ready": "就绪",
            "mac.status.saving": "保存中...",
            "mac.status.saved": "✓ MAC 地址已保存",
            "mac.status.edit_mode": "编辑模式 - 修改 MAC 地址后点击保存",
            "mac.status.cancelled": "已取消编辑",
            "mac.status.http": "HTTP 错误:",
            "mac.status.error": "错误",
            "mac.error.invalid_mac": "无效的 MAC 地址格式",
            "mac.error.no_mac": "没有可保存的 MAC 地址数据",
            "mac.error.invalid_response": "无效的服务器响应",
            "mac.action.modify": "修改",
            "mac.action.save": "保存",
            "mac.action.cancel": "取消",
            "mac.warn.1": "修改前请先使用分区备份功能备份 MAC 地址所在分区。",
            "mac.warn.2": "这些只是分区中的原始 MAC 数据，是否使用及如何使用均由固件决定。",
            "mac.warn.3": "修改后需重置或重刷固件才能识别修改后的 MAC 地址。",
            "mac.warn.4": "无效的 MAC 地址可能导致网络问题。",
            "mac.warn.uboot.1": "\u6b64\u64cd\u4f5c\u53ea\u4fee\u6539 APPSBLENV \u4e2d U-Boot \u7684 ethaddr\uff0c\u4e0d\u4fee\u6539 ART \u539f\u59cb\u6570\u636e\u3002",
            "mac.warn.uboot.2": "\u5176\u4ed6 MAC \u5b57\u6bb5\u4fdd\u6301\u4e0d\u53d8\uff1b\u8fd9\u4e0d\u4ee3\u8868\u5df2\u786e\u8ba4 OEM \u56fa\u4ef6\u4f7f\u7528\u54ea\u4e9b\u5730\u5740\u3002",
            "mac.warn.uboot.3": "APPSBLENV \u5199\u5165\u5e76\u8bfb\u56de\u6821\u9a8c\u540e\u624d\u62a5\u544a\u6210\u529f\uff1b\u8fd0\u884c\u4e2d\u7684\u7f51\u7edc\u63a5\u53e3\u53ef\u80fd\u9700\u8981\u91cd\u542f\u3002",
            "mac.warn.uboot.4": "\u4ec5\u586b\u5199\u4e00\u4e2a\u6709\u6548\u7684\u5355\u64ad\u57fa\u7840\u5730\u5740\uff1b\u65e0\u6548 MAC \u5730\u5740\u53ef\u80fd\u5f71\u54cd\u7f51\u7edc\u3002",
            "mac.status.read_only": "\u5f53\u524d\u4e0d\u652f\u6301\u6301\u4e45\u4fee\u6539 MAC\u3002",
            "mac.status.verify_failed": "\u65e0\u6cd5\u901a\u8fc7\u8bfb\u56de\u786e\u8ba4\u5df2\u4fdd\u5b58\u7684 U-Boot \u57fa\u7840 MAC\u3002",
            "mibib.title": "MIBIB 重载",
            "mibib.hint": "在 <strong>9008</strong> 模式下，通过重载 <strong>MIBIB</strong> 来初始化 <strong>SMEM (Shared Memory) 分区信息</strong>。<br>请选择本地文件并点击 <strong>重载</strong> 按钮。",
            "mibib.reload": "重载",
            "mibib.reload_success_hint": "MIBIB 重载成功，请点击下方按钮跳转到 “系统信息” 页面查看 SMEM 相关信息是否正确。",
            "mibib.reload_success": "查看系统信息",
            "mibib.not_9008": "非 9008 启动模式，无法进行 MIBIB 重载！",
            "mibib.warn.1": "仅限于 9008 模式下使用。",
            "mibib.warn.2": "重载成功后，请检查 SMEM 相关信息是否正确。",
            "network.title": "网络设置",
            "network.hint": "配置 <strong>U-Boot 网络参数</strong>（ipaddr、netmask、serverip）。<br>更改将保存到环境变量，重启后生效。",
            "network.label.ipaddr": "IP 地址:",
            "network.label.netmask": "子网掩码:",
            "network.label.serverip": "服务器 IP:",
            "network.action.refresh": "刷新",
            "network.action.save": "保存",
            "network.action.reset": "重置为默认值",
            "network.status.loading": "正在加载网络设置...",
            "network.status.ready": "就绪",
            "network.status.saving": "保存中...",
            "network.status.saved": "✓ 网络设置已保存",
            "network.status.reset": "✓ 已重置为默认值",
            "network.status.http": "HTTP 错误:",
            "network.status.error": "错误",
            "network.confirm.reset": "确定将所有网络设置重置为默认值？",
            "network.validation.valid": "有效",
            "network.validation.invalid_ip": "无效的 IP 地址格式",
            "network.validation.invalid_netmask": "无效的子网掩码格式",
            "network.validation.cannot_be_empty": "不能为空",
            "network.validation.invalid_format": "格式无效",
            "network.validation.invalid_netmask_format": "子网掩码格式无效",
            "network.warn.1": "修改网络设置可能影响网络连接。",
            "network.warn.2": "保存过程中请勿断电。",
            "network.warn.3": "无效的网络设置可能导致无法通过网络访问 U-Boot。",
            "settings.title": "设置",
            "settings.hint": "配置 <strong>应用偏好</strong>，包括语言和主题。",
            "settings.label.language": "语言",
            "settings.label.theme": "主题",
            "settings.theme.auto": "自动",
            "settings.theme.light": "亮色",
            "settings.theme.dark": "暗色",
            "settings.warn.1": "语言和主题设置保存在浏览器的本地存储中。",
            "settings.warn.2": "主题更改立即生效，自动模式跟随系统偏好。",
            "sysinfo.title": "系统信息",
            "sysinfo.title.board_info": "设备信息",
            "sysinfo.title.flash_info": "存储信息",
            "sysinfo.title.partitions_info": "分区信息",
            "sysinfo.loading": "正在加载系统信息...",
            "sysinfo.error.load": "加载系统信息失败",
            "sysinfo.error.parse": "解析系统信息失败",
            "sysinfo.uboot_version": "U-Boot 版本",
            "sysinfo.hostname": "主机名",
            "sysinfo.build_name": "编译配置名称",
            "sysinfo.family": "硬件系列",
            "sysinfo.model": "型号",
            "sysinfo.compatible": "兼容平台",
            "sysinfo.machid": "机器 ID",
            "sysinfo.ram_size": "内存大小",
            "sysinfo.smeminfo": "SMEM 信息",
            "sysinfo.flash_type": "闪存类型",
            "sysinfo.flash_block_size": "块大小",
            "sysinfo.flash_density": "容量密度",
            "sysinfo.bus": "\u603b\u7ebf",
            "sysinfo.chip_select": "\u7247\u9009",
            "sysinfo.present": "存在",
            "sysinfo.name": "名称",
            "sysinfo.size": "大小",
            "sysinfo.page_size": "页大小",
            "sysinfo.sector_size": "扇区大小",
            "sysinfo.erase_size": "擦除块大小",
            "sysinfo.block_size": "块大小",
            "sysinfo.oob_size": "OOB 大小",
            "sysinfo.oob_avail": "可用 OOB",
            "sysinfo.ecc_step_size": "ECC 步长",
            "sysinfo.ecc_strength": "ECC 强度",
            "sysinfo.vendor": "厂商",
            "sysinfo.product": "产品",
            "sysinfo.version": "版本",
            "sysinfo.emmc_life": "MMC 寿命信息",
            "sysinfo.status.normal": "正常",
            "sysinfo.status.warning": "警告",
            "sysinfo.status.urgent": "危险",
            "sysinfo.status.unknown": "未知",
            "sysinfo.type": "类型",
            "sysinfo.part_index": "序号",
            "sysinfo.part_start": "起始地址",
            "sysinfo.part_end": "结束地址",
            "sysinfo.no_data": "无数据",
            "sysinfo.boot_mode": "启动模式",
            "syslog.title": "系统日志",
            "syslog.copy": "复制",
            "syslog.clear": "清空",
            "syslog.save": "保存",
            "syslog.auto_scroll": "自动滚动",
            "syslog.confirm.clear": "是否清空所有日志？",
            "syslog.status.copied": "已复制到剪贴板",
            "syslog.status.copy_failed": "复制到剪贴板失败",
            "syslog.status.ready": "就绪",
            "syslog.status.cleared": "已清空",
            "syslog.status.saved": "保存完成",
            "syslog.status.no_content": "无日志内容",
            "syslog.status.auto_scroll_on": "自动滚动已开启",
            "syslog.status.auto_scroll_off": "自动滚动已关闭",
            "syslog.status.http": "HTTP 错误：",
            "syslog.warn.1": "日志通过轮询方式获取并自动更新。",
            "syslog.warn.2": "日志保存在浏览器会话存储中，关闭页面后将清空。",
            "reboot.confirm": "确认立即重启设备？",
            "reboot.title.in_progress": "正在重启设备",
            "reboot.hint.in_progress": "已发送重启请求，请稍候…<br>该页面短时间可能显示无响应，这是正常现象。",
            "initramfs.title": "INITRAMFS 启动",
            "initramfs.hint": "你将要在此设备上启动 <strong>Initramfs（内存固件）<\/strong>。<br>请选择本地文件并点击 <strong>上传<\/strong> 按钮。",
            "initramfs.boot_hint": "如果以上信息确认无误，请点击 “启动”。",
            "initramfs.warn.1": "如果一切顺利，设备将启动至内存固件。",
            "initramfs.warn.2": t["zh-cn"].warnChoose("内存固件"),
            "flashing.title.in_progress": "正在刷写",
            "flashing.hint.in_progress": "文件上传成功！正在执行刷写，请耐心等待。<br>刷写时间取决于镜像大小，可能需要几分钟。",
            "flashing.title.done": "刷写完成",
            "flashing.hint.done": "设备已成功更新！正在重启…",
            "flashing.hint.continue": "设备已成功更新！",
            "flashing.msg.continue": "当前仍处于 U-Boot Web 恢复模式，请继续执行其他操作或手动重启设备。",
            "booting.title.in_progress": "正在启动内存固件",
            "booting.hint.in_progress": "文件上传成功！正在启动，请稍候…<br>该页面短时间可能显示无响应，这是正常现象。",
            "booting.title.done": "启动成功",
            "booting.hint.done": "设备已成功进入内存固件！",
            "404.title": "页面不存在",
            "404.hint": "你访问的页面不存在！",
            "upload.title.in_progress": "正在上传",
            "upload.title.done": "上传完成",
            "fail.title.validate": "验证失败",
            "fail.title.update": "更新失败",
            "fail.hint.validate": "文件未通过验证。",
            "fail.hint.update": "更新过程中出现错误。",
            "error.title": "错误",
            "metadata.error.stale_commit": "\u4e00\u6b21\u6027\u4ee4\u724c\u5df2\u8fc7\u671f\u6216\u4e0e\u6b64\u5143\u6570\u636e\u4e0a\u4f20\u4e0d\u5339\u914d\uff0c\u8bf7\u91cd\u65b0\u4e0a\u4f20\u5e76\u9a8c\u8bc1\u3002",
            "metadata.error.staged_invalid": "\u7f13\u5b58\u7684\u955c\u50cf\u672a\u901a\u8fc7\u9a8c\u8bc1\uff0c\u8bf7\u91cd\u65b0\u4e0a\u4f20\u5e76\u9a8c\u8bc1\u3002",
            "metadata.error.bytes_changed": "\u7f13\u5b58\u6570\u636e\u4e0e\u9a8c\u8bc1\u65f6\u4e0d\u540c\uff0c\u8bf7\u91cd\u65b0\u9a8c\u8bc1\uff0c\u4e0d\u8981\u76f4\u63a5\u91cd\u8bd5\u3002",
            "metadata.error.layout_changed": "\u5df2\u9a8c\u8bc1\u7684\u5206\u533a\u5e03\u5c40\u5df2\u53d8\u66f4\uff0c\u8bf7\u91cd\u65b0\u8bfb\u53d6\u5e03\u5c40\u5e76\u9a8c\u8bc1\u3002",
            "metadata.error.write_failed": "\u5143\u6570\u636e\u5199\u5165\u6216\u8bfb\u56de\u6821\u9a8c\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u8bbe\u5907\u540e\u518d\u51b3\u5b9a\u662f\u5426\u518d\u6b21\u64cd\u4f5c\u3002",
            "metadata.error.staging_failed": "\u7cfb\u7edf\u65e0\u6cd5\u9a8c\u8bc1\u4e13\u7528\u7f13\u5b58\u533a\uff0c\u8bf7\u68c0\u67e5\u8bbe\u5907\u540e\u518d\u51b3\u5b9a\u662f\u5426\u518d\u6b21\u64cd\u4f5c\u3002",
            "metadata.error.uboot_partial": "U-Boot \u526f\u672c\u5199\u5165\u6216\u6821\u9a8c\u5931\u8d25\u3002\u8bf7\u5728\u65ad\u7535\u524d\u4e0b\u8f7d\u4e24\u4efd\u539f\u59cb\u5907\u4efd\uff0c\u672c\u6b21\u542f\u52a8\u5df2\u9501\u5b9a\u540e\u7eed U-Boot \u66f4\u65b0\u3002",
            "metadata.error.uboot_locked": "\u4e0a\u6b21 U-Boot \u66f4\u65b0\u672a\u5b8c\u6574\u5b8c\u6210\u3002\u8bf7\u4e0b\u8f7d\u539f\u59cb\u5907\u4efd\u5e76\u68c0\u67e5\u4e24\u4efd\u526f\u672c\uff0c\u518d\u51b3\u5b9a\u662f\u5426\u91cd\u542f\u3002",
            "metadata.error.single_needs_slot1": "大分区只能从物理第1区（0x900000）启动。请先把默认启动槽设为物理第1区，再重新验证。",
            "metadata.error.uboot_preflight": "U-Boot 写入前检查失败，两份副本均未擦除。请检查分区布局、NAND 和可用内存。",
            "metadata.error.uboot_failed": "U-Boot 更新在擦除前停止。请检查设备并重新验证文件。",
            "metadata.backup.primary": "\u4e0b\u8f7d\u539f APPSBL \u5907\u4efd",
            "metadata.backup.secondary": "\u4e0b\u8f7d\u539f APPSBL_1 \u5907\u4efd",
            "metadata.error.staging_unavailable": "\u5143\u6570\u636e\u6682\u5b58\u533a\u4e0d\u53ef\u7528\uff0c\u8bf7\u68c0\u67e5\u53ef\u7528\u5185\u5b58\u540e\u91cd\u65b0\u9a8c\u8bc1\u6587\u4ef6\u3002",
            "error.label.file_type": "文件类型",
            "error.label.file_size": "文件大小",
            "error.label.part_name": "分区名称",
            "error.label.part_size": "分区大小",
            "error.label.expected_type": "期望类型",
            "error.label.actual_type": "实际类型",
            "error.label.flash_type": "闪存类型",
            "error.label.filename": "文件名",
            "error.label.cmd": "命令",
            "error.label.cmd_output": "命令输出",
            "error.no_output": "无输出",
            "error.unit.bytes": "字节",
            "error.file_too_big": "文件过大",
            "error.part_not_found": "找不到分区",
            "error.wrong_file_type": "文件类型错误",
            "error.flash_not_found": "找不到闪存",
            "error.run_cmd_failed": "命令执行失败",
            "error.invalid_response": "无效的服务器响应",
            "error.suggestion.file_too_big": "请选择小于分区大小的文件或扩容分区。",
            "error.suggestion.part_not_found": "目标分区不存在或不可用。<br>请检查设备分区表或联系技术支持。",
            "error.suggestion.wrong_file_type": "请选择正确类型的文件。",
            "error.suggestion.flash_not_found": "找不到与所选文件类型对应的闪存设备。",
            "unknown": "未知"
        }
    };
})();

// RT3000-B firmware modes; all write targets come from the active layout API.
const firmwareModeManager = (() => {
    const isZh = () => APP_STATE.lang === 'zh-cn';
    const legacyFirmwareConfig = {
        formDataKey: 'firmware',
        warningItems: { common: true, custom: 1 }
    };

    async function fetchLayout() {
        const response = await fetch('/firmware/layout', { cache: 'no-store' });
        if (response.status === 404) return { legacy: true };
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        if (!data || typeof data !== 'object') throw new Error('Invalid layout response');
        if (!['dual', 'single', 'unsupported'].includes(data.mode)) data.mode = 'unsupported';
        if (!Array.isArray(data.partitions)) data.partitions = [];
        return data;
    }

    function frame(title, hint) {
        document.body.innerHTML = `
            <div class="app">
                <aside id="sidebar" class="sidebar"></aside>
                <div class="sidebar-overlay" id="sidebarOverlay"></div>
                <button class="sidebar-toggle" id="sidebarToggle"></button>
                <div class="main"><div id="m">
                    <div class="card-title-section"><h1 id="bFirmwareTitle"></h1></div>
                    <div class="card-hint-section"><p id="bFirmwareHint"></p></div>
                    <div class="card-main-section"><div class="card-main-inner" id="bFirmwareContent"></div></div>
                    <div class="card-foot-section"><div class="card-foot-inner" id="bFirmwareFoot"></div></div>
                </div><div id="version"></div></div>
            </div>`;
        document.title = title;
        document.getElementById('bFirmwareTitle').textContent = title;
        document.getElementById('bFirmwareHint').textContent = hint;
        ensureSidebar();
        const content = document.getElementById('bFirmwareContent');
        const foot = document.getElementById('bFirmwareFoot');
        return { content, foot };
    }

    function textNode(tag, text, className) {
        const node = document.createElement(tag);
        node.textContent = text;
        if (className) node.className = className;
        return node;
    }

    function layoutError(layout) {
        return layout && layout.error && typeof layout.error === 'string'
            ? layout.error : (isZh() ? '当前分区布局暂不支持此操作。' : 'The active partition layout is unsupported.');
    }

    function showLegacyFirmware() {
        const manager = getUnifiedUploadManager();
        manager.initForPage('firmware', legacyFirmwareConfig);
        ensureSidebar();
        applyI18n(document);
    }

    function navigationButton(label, href) {
        const button = textNode('button', label, 'button');
        button.type = 'button';
        button.onclick = () => { window.location.href = href; };
        return button;
    }

    function addWriteWarnings(foot) {
        const box = document.createElement('div');
        box.className = 'i w';
        box.appendChild(textNode('strong', isZh() ? '注意事项' : 'WARNINGS'));
        const list = document.createElement('ul');
        const warnings = isZh() ? [
            '刷写过程中请勿断电。',
            '请选择与设备型号和当前分区布局匹配的固件，并在写入前确认目标分区。'
        ] : [
            'Do not power off the device during update.',
            'Choose firmware matching the device model and active partition layout, then confirm the target partition before writing.'
        ];
        for (const warning of warnings) {
            list.appendChild(textNode('li', warning));
        }
        box.appendChild(list);
        foot.appendChild(box);
    }

    function addModeCard(parent, title, detail, enabled, href) {
        const card = document.createElement('div');
        card.className = 'upload-card upload-area';
        card.style.marginBottom = '1rem';
        card.appendChild(textNode('h2', title));
        card.appendChild(textNode('p', detail));
        if (enabled) {
            card.appendChild(navigationButton(isZh() ? '进入' : 'Open', href));
        } else {
            const button = textNode('button', isZh() ? '当前不可用' : 'Unavailable', 'button');
            button.type = 'button';
            button.disabled = true;
            card.appendChild(button);
        }
        parent.appendChild(card);
    }

    async function initLanding() {
        let layout;
        try {
            layout = await fetchLayout();
        } catch (error) {
            frame(isZh() ? '固件更新' : 'Firmware update', isZh() ? '无法读取当前分区布局。' : 'Could not read the active partition layout.');
            document.getElementById('bFirmwareContent').appendChild(textNode('p', error.message));
            return;
        }
        if (layout.legacy) {
            showLegacyFirmware();
            return;
        }
        const title = isZh() ? '固件更新模式' : 'Firmware update modes';
        const hint = isZh()
            ? '模式取决于当前已启用的分区表。此页面不会更改分区表。'
            : 'Modes follow the active partition table. This page does not change the table.';
        const { content, foot } = frame(title, hint);
        if (layout.status !== 'ok' || layout.mode === 'unsupported') {
            content.appendChild(textNode('p', layoutError(layout)));
            return;
        }
        const canWrite = layout.write_supported === true;
        const dualActive = layout.mode === 'dual';
        const singleActive = layout.mode === 'single';
        addModeCard(content,
            isZh() ? '双系统分区' : 'Dual rootfs slots',
            isZh() ? '选择 rootfs 或 rootfs_1 作为明确写入目标。' : 'Choose rootfs or rootfs_1 as the explicit write target.',
            canWrite && dualActive, '/firmware-dual.html');
        addModeCard(content,
            isZh() ? '单大分区' : 'Single large rootfs',
            singleActive
                ? (isZh() ? '当前活动分区表为单大分区布局。' : 'The active table uses the single large-rootfs layout.')
                : (isZh() ? '需先写入并启用匹配的大分区表（首版规划 80 MiB），然后重启。' : 'First install and activate a compatible large-rootfs table (initial plan: 80 MiB), then reboot.'),
            canWrite && singleActive, '/firmware-single.html');
        if (!canWrite) {
            content.appendChild(textNode('p', isZh() ? '当前引导环境尚未开放固件写入。' : 'Firmware writing is not enabled in this bootloader.'));
        }
        if (layout.ptable_write_supported === true) {
            content.appendChild(textNode('p', ptableSwitchNote()));
            content.appendChild(navigationButton(isZh() ? '打开 PTABLE 页面' : 'Open PTABLE page', '/ptable.html'));
        } else if (!singleActive && layout.ptable_write_supported === false) {
            const ptableNote = isZh()
                ? '当前版本暂未开放分区表写入。PTABLE 页面可查看，但不能在此转换布局。'
                : 'Partition-table writing is not enabled in this version. The PTABLE page is view-only for this workflow; it cannot convert the layout.';
            content.appendChild(textNode('p', ptableNote));
            content.appendChild(navigationButton(isZh() ? '查看 PTABLE 页面' : 'View PTABLE page', '/ptable.html'));
        }
        addWriteWarnings(foot);
    }

    function ptableSwitchNote() {
        return isZh()
            ? '切换双系统 / 大分区布局请到 PTABLE 页面生成并写入分区表：先在 Flash Backup 导出 rootfs 和 rootfs_1，写入后重启，再写入系统。'
            : 'To switch between the dual and big-partition layouts, generate and write the table on the PTABLE page: export rootfs and rootfs_1 from Flash Backup first, reboot after writing, then write a system.';
    }

    function validPartition(part) {
        if (!part || typeof part.name !== 'string' || !part.name.length) return false;
        const offset = Number(part.offset), size = Number(part.size);
        return Number.isSafeInteger(offset) && offset >= 0 &&
            Number.isSafeInteger(size) && size > 0 && Number.isSafeInteger(offset + size - 1);
    }

    function targetPartitions(layout, mode) {
        const expected = mode === 'dual' ? ['rootfs', 'rootfs_1'] : ['rootfs'];
        if (layout.partitions.length !== expected.length) return [];
        const candidates = layout.partitions.filter(p => p && expected.includes(p.name));
        if (candidates.length !== expected.length || candidates.some(p => !validPartition(p)) ||
            new Set(candidates.map(p => p.name)).size !== expected.length) return [];
        return expected.map(name => candidates.find(p => p.name === name));
    }

    function formatBytes(value) {
        const n = Number(value);
        return n.toLocaleString() + (isZh() ? ' 字节' : ' bytes') + ' (' + bytesToHuman(n) + ')';
    }

    function targetTitle(part) {
        return part.name;
    }

    function targetLocation(part, targets) {
        // Logical names can swap; number physical slots by their current address.
        const physical = [...targets].sort((a, b) => Number(a.offset) - Number(b.offset));
        return bootSlotLocationText({ ...part, physical_index: physical.indexOf(part) + 1 }, isZh());
    }

    function renderGate(title, reason, layout) {
        const { content, foot } = frame(title, isZh() ? '写入目标仅来自当前活动分区表。' : 'Targets are derived from the active partition table.');
        content.appendChild(textNode('p', reason));
        if (layout && layout.ptable_write_supported === true) {
            content.appendChild(textNode('p', ptableSwitchNote()));
            content.appendChild(navigationButton(isZh() ? '打开 PTABLE 页面' : 'Open PTABLE page', '/ptable.html'));
        } else if (layout && layout.ptable_write_supported === false) {
            content.appendChild(textNode('p', isZh()
                ? '当前版本暂未开放分区表写入；此页面不会创建或启用新布局。'
                : 'Partition-table writing is not enabled in this version. This page cannot create or activate a new layout.'));
            content.appendChild(navigationButton(isZh() ? '查看 PTABLE 页面' : 'View PTABLE page', '/ptable.html'));
        }
        foot.appendChild(textNode('p', isZh()
            ? '此模式不会修改分区表，也不会自动重启。'
            : 'This mode does not modify the partition table and does not reboot automatically.'));
    }

    function formatDescription(result) {
        const format = result && typeof result.format === 'string' ? result.format : '';
        if (format === 'h3c_rt3000_oem_ubi') {
            return isZh() ? 'H3C 原厂固件包' : 'H3C OEM firmware package';
        }
        if (format === 'ubi_unidentified') {
            return isZh()
                ? '完整 UBI 系统镜像（含内核和系统卷）'
                : 'Complete UBI system image (kernel and system volumes)';
        }
        return isZh() ? '未知格式：' + format.slice(0, 96) : 'Unknown format: ' + format.slice(0, 96);
    }

    function failureText(result, isWrite) {
        const code = result && result.info && typeof result.info.type === 'string' ? result.info.type : '';
        const raw = result && typeof result.error === 'string' ? result.error.slice(0, 180) : '';
        const alias = {'layout, mode, or target mismatch':'wrong_firmware_target','layout changed since validation':'layout_changed','unsupported package or invalid UBI image':'invalid_ubi_image','UBI capacity leaves insufficient layout and wear-level reserve':'insufficient_ubi_capacity','image exceeds selected extent':'image_too_large','write or full readback failed':'readback_failed','staged image no longer validates':'invalid_ubi_image','invalid token':'invalid_token','unknown or consumed token':'invalid_token','token, staged bytes, target, or layout changed':'upload_id_mismatch','staged upload range invalid':'upload_id_mismatch','staged upload changed':'upload_id_mismatch','target and mode required':'wrong_firmware_target','token, target, and mode required':'upload_id_mismatch','exactly one firmware field required':'firmware_field_invalid','invalid or oversized firmware field':'firmware_field_invalid'};
        const key = code || alias[raw] || raw;
        const en = {wrong_firmware_target:'Layout, mode, or target mismatch. Read the layout again.',layout_changed:'Layout changed; read it again and revalidate.',unsupported_firmware_format:'Unsupported format; choose a recognized H3C OEM package or complete UBI system image.',invalid_ubi_image:'Package is unsupported or the complete UBI system image is invalid.',insufficient_ubi_capacity:'UBI image leaves insufficient system-layout and wear-level reserve.',image_too_large:'File exceeds selected partition capacity.',upload_id_mismatch:'Validation token does not match uploaded file.',invalid_token:'Validation token is invalid or expired. Upload and validate again.',firmware_field_invalid:'Firmware file is empty, invalid, oversized, or duplicated.',write_not_verified:'Write or full readback failed. Inspect the device; do not retry automatically.',readback_failed:'Full readback failed. Inspect the device; do not retry automatically.',oem_firmware_needs_dual_layout:'The big partition accepts third-party systems only. Restore the OEM dual partition table to write OEM firmware.'};
        const zh = {wrong_firmware_target:'当前分区布局、模式或目标不匹配，请重新读取布局。',layout_changed:'验证后活动分区布局已变化，请重新读取并重新验证。',unsupported_firmware_format:'文件格式不支持；请选择已识别的 H3C OEM 包或完整 UBI 系统镜像。',invalid_ubi_image:'固件包不支持，或完整 UBI 系统镜像结构无效、不完整。',insufficient_ubi_capacity:'UBI 镜像无法为系统布局和磨损均衡保留足够空间。',image_too_large:'文件超过所选分区容量。',upload_id_mismatch:'验证令牌与上传文件不匹配。',invalid_token:'验证令牌无效或已过期，请重新上传并验证。',firmware_field_invalid:'固件文件为空、无效、过大或重复上传。',write_not_verified:'写入或完整读回校验未通过。请检查设备，不要自动重试。',readback_failed:'完整读回校验未通过。请检查设备，不要自动重试。',oem_firmware_needs_dual_layout:'大分区只接受第三方系统。要写原厂固件，请先切回原厂双系统分区表。'};
        const message = isZh() ? zh[key] : en[key];
        if (message) return message;
        if (raw) return isZh() ? '操作失败：' + raw : 'Operation failed: ' + raw;
        return isWrite ? (isZh() ? '写入失败或结果未验证。请检查设备，不要自动重试。' : 'Write failed or remains unverified. Inspect the device; do not retry automatically.')
            : (isZh() ? '固件验证失败，请检查文件。' : 'Firmware validation failed. Check the file.');
    }

    function postForm(url, form, onProgress) {
        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url);
            if (onProgress && xhr.upload) xhr.upload.addEventListener('progress', onProgress);
            xhr.onload = () => {
                let data;
                try { data = JSON.parse(xhr.responseText); }
                catch (error) { reject(new Error('Invalid server response')); return; }
                if (xhr.status < 200 || xhr.status >= 300) {
                    if (!data || typeof data !== 'object') data = {};
                    data.status = 'fail';
                    data.httpStatus = xhr.status;
                }
                resolve(data);
            };
            xhr.onerror = () => reject(new Error('Connection failed'));
            xhr.send(form);
        });
    }

    async function initMode(mode) {
        let layout;
        try { layout = await fetchLayout(); }
        catch (error) {
            renderGate(isZh() ? '固件更新' : 'Firmware update', error.message, null);
            return;
        }
        if (layout.legacy) {
            renderGate(isZh() ? '固件更新' : 'Firmware update',
                isZh() ? '此型号未提供 公共分区布局接口。' : 'The common layout API is unavailable on this model.', layout);
            return;
        }
        if (layout.status !== 'ok' || layout.mode !== mode) {
            const reason = mode === 'single'
                ? (isZh() ? '需先写入并启用匹配的大分区表（首版规划 80 MiB），然后重启；此页面不会创建或切换分区表。' : 'First install and activate a compatible large-rootfs table (initial plan: 80 MiB), then reboot. This page does not create or switch tables.')
                : (isZh() ? '当前活动布局不是双系统分区。' : 'The active layout is not dual-slot.');
            renderGate(isZh() ? '固件更新' : 'Firmware update', reason, layout);
            return;
        }
        if (layout.write_supported !== true) {
            renderGate(isZh() ? '固件更新' : 'Firmware update',
                isZh() ? '当前引导环境尚未开放固件写入。' : 'Firmware writing is not enabled in this bootloader.', layout);
            return;
        }
        const targets = targetPartitions(layout, mode);
        if (!targets.length) {
            renderGate(isZh() ? '固件更新' : 'Firmware update', layoutError({ error: '当前分区列表与所选模式不一致。' }), layout);
            return;
        }
        renderUpload(mode, layout, targets);
    }

    function renderUpload(mode, layout, targets) {
        const dual = mode === 'dual';
        const title = dual
            ? (isZh() ? '双系统固件写入' : 'Dual-slot firmware write')
            : (isZh() ? '单大分区固件写入' : 'Single large-rootfs firmware write');
        const hint = isZh()
            ? '先明确选择目标、上传并核验文件；确认后单独提交写入。不会自动重启。'
            : 'Select the target, upload and validate the image, then explicitly submit the write. No automatic reboot.';
        const { content, foot } = frame(title, hint);
        const targetTitleNode = textNode('h2', isZh() ? '写入目标' : 'Write target');
        content.appendChild(targetTitleNode);
        let selectedTarget = null;
        let selectedFile = null;
        let token = null;
        let validated = null;
        let validationBusy = false;
        let validationEpoch = 0;
        let writeStarted = false;

        const targetGroup = document.createElement('div');
        const targetInputs = [];
        targetGroup.className = 'upload-card upload-area';
        targets.forEach(part => {
            const card = document.createElement('label');
            card.style.display = 'block';
            card.style.padding = '0.75rem 0';
            card.style.borderBottom = '1px solid var(--border)';
            const radio = document.createElement('input');
            radio.type = 'radio';
            radio.name = 'firmware-target';
            radio.value = part.name;
            radio.checked = false;
            targetInputs.push(radio);
            radio.addEventListener('change', () => {
                if (!radio.checked) return;
                selectedTarget = part.name;
                invalidateValidation(isZh() ? '目标已更改，请重新验证文件。' : 'Target changed; validate the file again.');
                updateValidateButton();
            });
            card.appendChild(radio);
            card.appendChild(textNode('strong', targetTitle(part)));
            card.appendChild(textNode('div', targetLocation(part, targets)));
            const size = Number(part.size);
            card.appendChild(textNode('div', (isZh() ? '容量：' : 'Capacity: ') + formatBytes(size)));
            targetGroup.appendChild(card);
        });
        content.appendChild(targetGroup);

        const fileLabel = textNode('label', isZh() ? '固件文件' : 'Firmware file');
        const fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.name = 'firmware';
        fileInput.addEventListener('change', () => {
            selectedFile = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
            invalidateValidation(isZh() ? '文件已更改，请重新验证。' : 'File changed; validate it again.');
            updateValidateButton();
        });
        content.appendChild(fileLabel);
        content.appendChild(fileInput);
        const validateButton = textNode('button', isZh() ? '上传并验证' : 'Upload and validate', 'button');
        validateButton.type = 'button';
        validateButton.disabled = true;
        content.appendChild(validateButton);
        const review = document.createElement('div');
        review.className = 'upload-success';
        review.hidden = true;
        const reviewTitle = textNode('h3', isZh() ? '验证结果' : 'Validation result');
        const reviewDetails = document.createElement('div');
        const writeButton = textNode('button', isZh() ? '确认写入所选分区' : 'Confirm write to selected partition', 'button');
        writeButton.type = 'button';
        writeButton.disabled = true;
        review.appendChild(reviewTitle);
        review.appendChild(reviewDetails);
        review.appendChild(writeButton);
        content.appendChild(review);
        const status = textNode('p', '');
        content.appendChild(status);
        content.appendChild(navigationButton(isZh() ? '返回固件模式' : 'Back to firmware modes', '/firmware.html'));
        addWriteWarnings(foot);

        function invalidateValidation(message) {
            validationEpoch++;
            token = null;
            validated = null;
            review.hidden = true;
            reviewDetails.textContent = '';
            writeButton.disabled = true;
            writeStarted = false;
            status.textContent = message || '';
        }
        function updateValidateButton() {
            validateButton.disabled = !selectedFile || !selectedTarget || writeStarted || validationBusy;
        }
        function failCode(result, isWrite) {
            status.textContent = failureText(result, isWrite);
        }
        validateButton.addEventListener('click', async () => {
            if (!selectedFile || !selectedTarget || validateButton.disabled || validationBusy) return;
            const requestedFile = selectedFile;
            const requestedTarget = selectedTarget;
            invalidateValidation(isZh() ? '正在上传并验证…' : 'Uploading and validating…');
            const requestEpoch = validationEpoch;
            validationBusy = true;
            targetInputs.forEach(input => { input.disabled = true; });
            fileInput.disabled = true;
            validateButton.disabled = true;
            const form = new FormData();
            form.append('firmware', requestedFile);
            form.append('target', requestedTarget);
            form.append('mode', mode);
            const requestStillCurrent = () => requestEpoch === validationEpoch &&
                selectedFile === requestedFile && selectedTarget === requestedTarget;
            try {
                const result = await postForm('/firmware/validate', form, event => {
                    if (requestStillCurrent() && event.lengthComputable && event.total > 0) {
                        const percent = Math.floor(event.loaded * 100 / event.total);
                        status.textContent = (isZh() ? '上传并验证：' : 'Upload and validation: ') + percent + '%';
                    }
                });
                if (!requestStillCurrent()) return;
                const validatedTarget = typeof result.target === 'string' ? result.target : result.target && result.target.name;
                if (result.status !== 'ok') { failCode(result, false); return; }
                if (typeof result.token !== 'string' || !result.token || validatedTarget !== requestedTarget ||
                    result.mode !== mode) {
                    status.textContent = isZh() ? '验证响应与所选目标不匹配。' : 'Validation response does not match the selected target.';
                    return;
                }
                token = result.token;
                validated = { target: requestedTarget, mode, file: requestedFile, response: result };
                reviewDetails.textContent = '';
                reviewDetails.appendChild(textNode('p', (isZh() ? '目标：' : 'Target: ') + targetTitle(targets.find(p => p.name === requestedTarget))));
                reviewDetails.appendChild(textNode('p', targetLocation(targets.find(p => p.name === requestedTarget), targets)));
                if (typeof result.bytes === 'number') reviewDetails.appendChild(textNode('p', (isZh() ? '文件大小：' : 'File size: ') + formatBytes(result.bytes)));
                if (typeof result.sha256 === 'string') reviewDetails.appendChild(textNode('p', 'SHA-256: ' + result.sha256));
                if (typeof result.format === 'string') reviewDetails.appendChild(textNode('p', (isZh() ? '格式：' : 'Format: ') + formatDescription(result)));
                review.hidden = false;
                writeButton.disabled = false;
                status.textContent = isZh() ? '文件已验证。检查目标与摘要后，明确提交写入。' : 'Image validated. Review the target and digest, then explicitly submit the write.';
            } catch (error) {
                if (requestStillCurrent()) invalidateValidation((isZh() ? '上传或验证失败：' : 'Upload or validation failed: ') + error.message);
            } finally {
                validationBusy = false;
                if (!writeStarted) {
                    targetInputs.forEach(input => { input.disabled = false; });
                    fileInput.disabled = false;
                }
                updateValidateButton();
            }
        });
        writeButton.addEventListener('click', async () => {
            if (!token || !validated || writeStarted || validated.target !== selectedTarget || validated.mode !== mode || validated.file !== selectedFile) return;
            writeStarted = true;
            const writeToken = token;
            token = null;
            validateButton.disabled = true;
            writeButton.disabled = true;
            targetInputs.forEach(input => { input.disabled = true; });
            fileInput.disabled = true;
            status.textContent = isZh() ? '正在写入并等待后端校验结果…' : 'Writing; waiting for the backend verification result…';
            const form = new FormData();
            form.append('token', writeToken);
            form.append('target', selectedTarget);
            form.append('mode', mode);
            try {
                const result = await postForm('/firmware/write', form);
                const writtenTarget = typeof result.target === 'string' ? result.target : result.target && result.target.name;
                if (result.status !== 'ok' || result.verified !== true || writtenTarget !== selectedTarget || result.mode !== mode) {
                    failCode(result, true);
                    status.textContent += isZh() ? ' 请检查设备；页面不会自动重试或重启。' : ' Inspect the device; this page will not retry or reboot.';
                    return;
                }
                status.textContent = isZh()
                    ? '写入及后端校验已完成。没有自动重启。'
                    : 'Write and backend verification completed. No automatic reboot was requested.';
                reviewDetails.appendChild(textNode('p', (isZh() ? '已验证写入目标：' : 'Verified write target: ') + targetTitle(targets.find(p => p.name === writtenTarget))));
            } catch (error) {
                status.textContent = (isZh()
                    ? '无法确认写入结果：' : 'Write result could not be confirmed: ') + error.message +
                    (isZh() ? ' 请检查设备；不要自动重试或重启。' : ' Inspect the device; do not retry or reboot automatically.');
            }
        });
    }

    return { initLanding, initMode };
})();

// Boot card version rows appear only when the backend classification supports them.
function bootSlotFirmwareText(slot, chinese, hwC = false) {
    if (!slot) return null;
    if (hwC && slot.known !== true) return chinese ? '系统身份：未识别' : 'System identity: unrecognized';
    if (slot.known !== true) return null;
    if (slot.type === 'OEM' && typeof slot.firmware_version === 'string' &&
        slot.firmware_version.trim())
        return (chinese ? '\u56fa\u4ef6\uff1a' : 'Firmware: ') + slot.firmware_version;
    if (slot.detection === 'rootfs_release_file' && typeof slot.version === 'string' && slot.version.trim())
        return (chinese ? '系统：第三方系统（' + slot.version + '）' : 'System: Third-party system (' + slot.version + ')');
    if (slot.type === 'OpenWrt' && typeof slot.version === 'string')
        return (chinese ? '系统：第三方系统（' + slot.version + '）' : 'System: Third-party system (' + slot.version + ')');
    if (slot.type && slot.type !== 'unknown' && slot.type !== 'OEM')
        return (chinese ? '\u56fa\u4ef6\uff1a\u7b2c\u4e09\u65b9\u56fa\u4ef6' : 'Firmware: Third-party firmware');
    return null;
}
function bootSlotLocationText(slot, chinese) {
    const raw = slot && slot.offset;
    const offset = typeof raw === 'number' ? raw :
        (typeof raw === 'string' && /^0x[0-9a-f]+$/i.test(raw) ? Number(raw) : NaN);
    if (!Number.isSafeInteger(offset) || offset < 0) return null;
    const index = slot.physical_index;
    const region = Number.isInteger(index) && index >= 1 && index <= 2
        ? (chinese ? '物理第 ' + index + ' 区 · ' : 'Physical slot ' + index + ' · ') : '';
    const size = Number(slot.size);
    if (Number.isSafeInteger(size) && size > 0 && Number.isSafeInteger(offset + size - 1)) {
        // Display an inclusive end address, shared by overview and write targets.
        return region + (chinese ? '地址范围：' : 'Address range: ') +
            '0x' + offset.toString(16) + ' – 0x' + (offset + size - 1).toString(16);
    }
    return region + (chinese ? '起始地址：' : 'Start: ') + '0x' + offset.toString(16);
}
function bootSlotCapacityText(slot, chinese) {
    const raw = slot && slot.size; let bytes = null;
    if (typeof raw === 'number') bytes = Number.isSafeInteger(raw) && raw > 0 ? raw : null;
    else if (typeof raw === 'string') {
        if (/^0x[0-9a-f]+$/i.test(raw)) bytes = Number.parseInt(raw.slice(2), 16);
        else if (/^[0-9]+$/.test(raw)) bytes = Number(raw);
        if (!Number.isSafeInteger(bytes) || bytes <= 0) bytes = null;
    }
    if (bytes === null) return null;
    return (chinese ? '\u5bb9\u91cf\uff1a' : 'Capacity: ') + bytes.toLocaleString() +
        (chinese ? ' \u5b57\u8282' : ' bytes');
}

// RT3000-B partition boot controls; reuse the upstream overview/card styles.
const bootSlotsManager = (() => {
    const zh = () => APP_STATE.lang === 'zh-cn';
    const isHwC = () => APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.family === 'HW-C';
    let refreshBusy = false, operationBusy = false, refreshTimer = null;
    let active = true, listenersReady = false, lastSignature = null;
    const signature = (data, saved) => JSON.stringify([data.mode, data.fingerprint,
        data.environment_restored, data.slots, saved]);
    function scheduleRefresh() {
        if (!active || refreshTimer !== null || typeof setTimeout !== 'function') return;
        refreshTimer = setTimeout(async () => {
            refreshTimer = null;
            if (active && !document.hidden) await init(false);
            scheduleRefresh();
        }, 5000);
    }
    function renderDReadonlySlots(section, data) {
        const inner = section.querySelector('.card-main-inner');
        inner.textContent = '';
        const title = document.createElement('h2');
        title.textContent = zh() ? '只读槽位识别' : 'Read-only slot identification';
        inner.appendChild(title);
        const notice = document.createElement('p');
        notice.textContent = zh()
            ? (document.body.classList.contains('nx30-d-nand-readonly') ? 'NX30 HW-D 只读维护：查看槽位与固件内容。' : 'NX30 HW-D RAM 预览：仅识别槽位与内容；启动、设默认和写入功能均关闭。')
            : (document.body.classList.contains('nx30-d-nand-readonly') ? 'NX30 HW-D read-only maintenance: inspect slots and firmware contents.' : 'NX30 HW-D RAM preview: slot/content identification only; boot, default selection, and writes are disabled.');
        inner.appendChild(notice);
        const layout = document.createElement('p');
        layout.textContent = (zh() ? '检测到的布局：' : 'Detected layout: ') +
            (data.mode || 'unknown') + (data.fingerprint ? ' · ' + data.fingerprint : '');
        inner.appendChild(layout);
        if (data.error) {
            const error = document.createElement('p');
            error.textContent = (zh() ? '布局未识别：' : 'Layout not identified: ') + data.error;
            inner.appendChild(error);
        }
        const table = document.createElement('table');
        table.className = 'table';
        const header = document.createElement('tr');
        for (const label of (zh() ? ['分区', '物理位置', '识别结果'] : ['Partition', 'Physical extent', 'Identification'])) {
            const th = document.createElement('th'); th.textContent = label; header.appendChild(th);
        }
        table.appendChild(header);
        for (const slot of data.slots) {
            if (slot.name !== 'rootfs' && slot.name !== 'rootfs_1') continue;
            const row = document.createElement('tr');
            const name = document.createElement('td'); name.textContent = slot.name; row.appendChild(name);
            const extent = document.createElement('td');
            extent.textContent = (zh() ? '物理第 ' : 'Physical ') + (slot.physical_index || '?') +
                ' · ' + (slot.offset || '?') + ' · ' + (slot.size || '?');
            row.appendChild(extent);
            const result = document.createElement('td');
            result.textContent = [slot.label, slot.version, slot.status, slot.error].filter(Boolean).join(' · ') || 'unknown';
            row.appendChild(result);
            table.appendChild(row);
        }
        inner.appendChild(table);
        section.hidden = false;
    }

    function addBackupLinks(parent) {
        const box = document.createElement('div');
        const names = [
            ['/boot/default/backup/bootconfig.bin', zh() ? '下载原 BOOTCONFIG 备份' : 'Download original BOOTCONFIG backup'],
            ['/boot/default/backup/bootconfig1.bin', zh() ? '下载原 BOOTCONFIG1 备份' : 'Download original BOOTCONFIG1 backup']
        ];
        for (const [href, text] of names) {
            const link = document.createElement('a');
            link.href = href;
            link.download = href.endsWith('bootconfig.bin') ? 'bootconfig-before.bin' : 'bootconfig1-before.bin';
            link.textContent = text;
            box.appendChild(link);
        }
        parent.appendChild(box);
    }
    async function init(force = true) {
        const section = document.getElementById('boot_slots');
        if (!section || refreshBusy || operationBusy) return;
        if (!listenersReady && typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
            listenersReady = true;
            window.addEventListener('pagehide', () => {
                active = false;
                if (refreshTimer !== null && typeof clearTimeout === 'function') clearTimeout(refreshTimer);
                refreshTimer = null;
            });
            if (typeof document.addEventListener === 'function')
                document.addEventListener('visibilitychange', () => { if (!document.hidden) init(); });
        }
        refreshBusy = true;
        try {
            const dReadonly = document.body.classList.contains("nx30-d-ram-readonly");
            const [slotsResponse, defaultResponse] = await Promise.all([
                fetch('/boot/slots', { cache: 'no-store' }),
                dReadonly ? Promise.resolve(null) : fetch('/boot/default', { cache: 'no-store' }).catch(() => null)
            ]);
            if (slotsResponse.status === 404) return;
            if (!slotsResponse.ok) throw new Error('HTTP ' + slotsResponse.status);
            const data = await slotsResponse.json();
            if (!Array.isArray(data.slots)) throw new Error('Invalid slot response');
            let saved = null;
            if (defaultResponse && defaultResponse.ok) saved = await defaultResponse.json();
            const freshSignature = signature(data, saved);
            if (!force && freshSignature === lastSignature) return;
            lastSignature = freshSignature;
            if (dReadonly) {
                renderDReadonlySlots(section, data);
                return;
            }
            const inner = section.querySelector('.card-main-inner');
            inner.textContent = '';
            const title = document.createElement('h2');
            title.textContent = zh() ? '按分区启动' : 'Boot partition';
            inner.appendChild(title);
            const refresh = document.createElement('button');
            refresh.type = 'button'; refresh.className = 'button';
            refresh.textContent = zh() ? '刷新槽位信息' : 'Refresh slot information';
            refresh.onclick = () => init();
            inner.appendChild(refresh);
            const hint = document.createElement('p');
            hint.textContent = saved && saved.default_applies_to === 'bootipq' && saved.auto_web === false
                ? (zh()
                    ? '上电将启动已保存的默认分区；上电时按住 RESET 约 3 秒进入维护网页。'
                    : 'Power-on boots the saved default partition. At power-on, hold RESET for about 3 seconds to enter the maintenance page.')
                : saved && saved.auto_web === true && saved.default_applies_to === 'bootipq'
                    ? (zh()
                        ? '上电仍进入维护网页；保存默认系统只更改分区启动默认选择，不会改变开机入口或立即重启。'
                        : 'Power-on still opens the maintenance page. Saving a default changes only the partition default; it does not change the power-on entry or reboot now.')
                    : (zh()
                        ? '启动仅本次有效；设为默认会在下次开机使用，不会立即启动。'
                        : 'Boot applies once. Set as default takes effect at the next startup and does not reboot now.');
            if (data.read_only === true) {
                const preview = document.createElement('p');
                preview.textContent = zh() ? 'RAM 只读预览：启动和写入操作已禁用。' : 'RAM read-only preview: boot and write actions are disabled.';
                inner.appendChild(preview);
            }
            inner.appendChild(hint);
            const table = document.createElement('table');
            table.className = 'table';
            const status = document.createElement('p');
            const defaultStatus = document.createElement('p');
            const bootButtons = [];
            const defaultChecks = {};
            let currentDefault = saved && (saved.default === 'rootfs' || saved.default === 'rootfs_1')
                ? saved.default : null;
            let defaultsLocked = !saved || saved.can_change !== true || saved.locked === true || data.read_only === true || data.boot_supported === false;
            for (const slot of data.slots) {
                if (slot.name !== 'rootfs' && slot.name !== 'rootfs_1') continue;
                const row = document.createElement('tr');
                row.className = 'tr';
                const name = document.createElement('td');
                name.className = 'td left';
                name.textContent = slot.name;
                const location = bootSlotLocationText(slot, zh());
                const locationLine = document.createElement('div');
                locationLine.textContent = location || (zh() ? '当前地址不可用' : 'Current address unavailable');
                name.appendChild(locationLine);
                const capacity = bootSlotCapacityText(slot, zh());
                if (capacity) {
                    const capacityLine = document.createElement('div');
                    capacityLine.textContent = capacity;
                    name.appendChild(capacityLine);
                }
                const firmwareText = bootSlotFirmwareText(slot, zh(), isHwC());
                if (firmwareText) {
                    const firmwareLine = document.createElement('div');
                    firmwareLine.textContent = firmwareText;
                    name.appendChild(firmwareLine);
                }
                const action = document.createElement('td');
                action.className = 'td right';
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'button';
                button.textContent = (zh() ? '启动 ' : 'Boot ') + slot.name;
                button.disabled = data.read_only === true || data.boot_supported === false;
                bootButtons.push(button);
                button.onclick = async () => {
                    if (operationBusy) return;
                    operationBusy = true;
                    bootButtons.forEach(b => { b.disabled = true; });
                    status.textContent = (zh() ? '正在请求启动 ' : 'Requesting boot of ') + slot.name;
                    const form = new FormData();
                    form.append('cmd', 'bootipq ' + slot.name);
                    try {
                        const response = await fetch('/webterm/exec', { method: 'POST', body: form });
                        if (!response.ok) throw new Error('HTTP ' + response.status);
                        status.textContent = zh() ? '启动命令已提交，请观察设备。' : 'Boot command submitted. Observe the device.';
                        try {
                            const output = await response.text();
                            if (output.trim()) status.textContent = output.trim();
                            bootButtons.forEach(b => { b.disabled = false; });
                        } catch (e) { /* Kernel entry closes the U-Boot connection. */ }
                    } catch (e) {
                        status.textContent = zh() ? '连接已中断或请求失败，请观察设备状态。' : 'Connection closed or request failed. Check the device.';
                        bootButtons.forEach(b => { b.disabled = false; });
                    } finally { operationBusy = false; }
                };
                action.appendChild(button);

                const label = document.createElement('label');
                label.className = 'default-boot-label';
                label.style.display = 'inline-flex';
                label.style.alignItems = 'center';
                label.style.marginLeft = '10px';
                const check = document.createElement('input');
                check.type = 'checkbox';
                check.checked = slot.name === currentDefault;
                check.disabled = defaultsLocked;
                check.style.appearance = 'checkbox';
                check.setAttribute('aria-label', (zh() ? '设为默认 ' : 'Set as default ') + slot.name);
                defaultChecks[slot.name] = check;
                const labelText = document.createElement('span');
                labelText.textContent = zh() ? '设为默认' : 'Set as default';
                label.appendChild(labelText);
                label.appendChild(check);
                action.appendChild(label);
                check.addEventListener('change', async () => {
                    if (!check.checked) {
                        check.checked = slot.name === currentDefault;
                        return;
                    }
                    if (defaultsLocked) { check.checked = slot.name === currentDefault; return; }
                    const previous = currentDefault;
                    if (slot.name === currentDefault) { check.checked = true; return; }
                    if (operationBusy) { check.checked = false; return; }
                    operationBusy = true;
                    /* Keep the existing choice checked until the write succeeds. */
                    check.checked = false;
                    Object.values(defaultChecks).forEach(input => { input.disabled = true; });
                    defaultStatus.textContent = zh() ? '正在保存默认系统…' : 'Saving default system…';
                    const form = new FormData();
                    form.append('target', slot.name);
                    let partial = false;
                    try {
                        const response = await fetch('/boot/default', { method: 'POST', body: form });
                        const result = await response.json();
                        if (!response.ok || result.status !== 'ok') {
                            if (result.status === 'partial') { defaultsLocked = true; partial = true; }
                            throw new Error(result.error || 'HTTP ' + response.status);
                        }
                        currentDefault = result.default;
                        saved = Object.assign({}, saved, { default: currentDefault });
                        lastSignature = signature(data, saved);
                        Object.entries(defaultChecks).forEach(([name, input]) => {
                            input.checked = name === currentDefault;
                        });
                        defaultStatus.textContent = zh() ? '已保存，下次开机生效。' : 'Saved. This takes effect at the next startup.';
                        addBackupLinks(defaultStatus);
                    } catch (e) {
                        currentDefault = previous;
                        Object.entries(defaultChecks).forEach(([name, input]) => {
                            input.checked = name === currentDefault;
                        });
                        if (defaultsLocked) Object.values(defaultChecks).forEach(input => { input.disabled = true; });
                        defaultStatus.textContent = partial
                            ? (zh() ? 'BOOTCONFIG 可能部分更新，默认系统操作已锁定，请先检查设备。' : 'A BOOTCONFIG update may be partial; default selection is locked for inspection.')
                            : (zh() ? '保存失败：' : 'Save failed: ') + e.message;
                        if (partial) addBackupLinks(defaultStatus);
                        /* A timed-out response may follow a completed transaction. */
                        let stateRefreshed = false;
                        try {
                            const latestResponse = await fetch('/boot/default', { cache: 'no-store' });
                            if (latestResponse.ok) {
                                const latest = await latestResponse.json();
                                if (latest.default === 'rootfs' || latest.default === 'rootfs_1') {
                                    stateRefreshed = true;
                                    currentDefault = latest.default;
                                    Object.entries(defaultChecks).forEach(([name, input]) => { input.checked = name === currentDefault; });
                                    defaultsLocked = partial || latest.can_change !== true || latest.locked === true;
                                    if (defaultsLocked) Object.values(defaultChecks).forEach(input => { input.disabled = true; });
                                    if (currentDefault !== previous) addBackupLinks(defaultStatus);
                                }
                            }
                        } catch (refreshError) { /* Keep the pre-request check state. */ }
                        if (!stateRefreshed) {
                            currentDefault = null;
                            defaultsLocked = true;
                            Object.values(defaultChecks).forEach(input => { input.checked = false; input.disabled = true; });
                        }
                    } finally {
                        operationBusy = false;
                        if (!defaultsLocked) Object.values(defaultChecks).forEach(input => { input.disabled = false; });
                    }
                });
                row.appendChild(name);
                row.appendChild(action);
                table.appendChild(row);
            }
            inner.appendChild(table);
            inner.appendChild(status);
            inner.appendChild(defaultStatus);
            if (defaultsLocked) {
                defaultStatus.textContent = saved && saved.reason === 'readonly_profile'
                    ? (zh() ? '当前只读预览不允许保存默认系统。' : 'The read-only preview cannot save a default system.')
                    : (zh()
                        ? (saved && saved.reason === 'partial_update' ? 'BOOTCONFIG 部分更新，默认系统操作已锁定，请先检查设备。' : '当前启动命令或存储状态不支持更改默认系统。')
                        : (saved && saved.reason === 'partial_update' ? 'A BOOTCONFIG update is partial; default selection is locked for inspection.' : 'The current boot state does not allow a safe default change.'));
                if (saved && saved.reason === 'partial_update') addBackupLinks(defaultStatus);
            }
            section.hidden = false;
        } catch (e) {
            section.hidden = false;
            section.querySelector('.card-main-inner').textContent = (zh() ? '分区信息读取失败：' : 'Partition detection failed: ') + e.message;
            lastSignature = null;
        } finally {
            refreshBusy = false;
            scheduleRefresh();
        }
    }
    return { init };
})();

// Physical button status on RT3000-B, using the original overview card style.
const boardButtonsManager = (() => {
    const zh = () => APP_STATE.lang === 'zh-cn';
    let active = true;
    async function init() {
        const section = document.getElementById('board_buttons');
        if (!section) return;
        const inner = section.querySelector('.card-main-inner');
        const title = document.createElement('h2');
        title.textContent = zh() ? '按键检测' : 'Button status';
        const hint = document.createElement('p');
        hint.textContent = zh()
            ? '长按 RESET 约 3 秒进入救援页面；WPS/MESH 仅显示状态。'
            : 'Hold RESET for about 3 seconds to enter recovery; WPS and MESH only show status.';
        const table = document.createElement('table');
        table.className = 'table';
        const message = document.createElement('p');
        inner.appendChild(title);
        inner.appendChild(hint);
        inner.appendChild(table);
        inner.appendChild(message);
        window.addEventListener('pagehide', () => { active = false; });
        async function refresh() {
            if (!active) return;
            try {
                const response = await fetch('/board/buttons', { cache: 'no-store' });
                if (response.status === 404) return;
                if (!response.ok) throw new Error('HTTP ' + response.status);
                const data = await response.json();
                if (!Array.isArray(data.buttons)) throw new Error('Invalid button response');
                table.textContent = '';
                for (const button of data.buttons) {
                    if (button.name !== 'RESET' && button.name !== 'WPS/MESH') continue;
                    const row = document.createElement('tr');
                    row.className = 'tr';
                    const name = document.createElement('td');
                    name.className = 'td left';
                    name.textContent = button.name;
                    const state = document.createElement('td');
                    state.className = 'td right';
                    state.textContent = button.pressed
                        ? (zh() ? '按下' : 'Pressed')
                        : (zh() ? '松开' : 'Released');
                    row.appendChild(name);
                    row.appendChild(state);
                    table.appendChild(row);
                }
                message.textContent = '';
                section.hidden = false;
            } catch (error) {
                section.hidden = false;
                message.textContent = (zh() ? '按键状态读取失败：' : 'Button status unavailable: ') + error.message;
            }
            if (active) setTimeout(refresh, 500);
        }
        await refresh();
    }
    return { init };
})();
