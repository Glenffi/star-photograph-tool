(() => {
    const platformFromBrowser = () => {
        const value = `${navigator.platform || ""} ${navigator.userAgent || ""}`
            .toLowerCase();
        if (value.includes("win")) return "windows-x64";
        if (value.includes("mac")) return "macos-arm64";
        return "windows-x64";
    };

    const humanSize = (bytes) => {
        const mebibytes = Number(bytes) / (1024 * 1024);
        return Number.isFinite(mebibytes)
            ? `${mebibytes.toFixed(mebibytes < 10 ? 1 : 0)} MiB`
            : "";
    };

    const recommendedPlatform = platformFromBrowser();
    const recommendedCard = document.querySelector(
        `[data-platform-card="${recommendedPlatform}"]`
    );
    recommendedCard?.classList.add("is-recommended");

    const setRecommendedDownload = (url) => {
        const button = document.querySelector('[data-download="recommended"]');
        if (!button || !url) return;
        button.href = url;
        button.textContent = recommendedPlatform === "macos-arm64"
            ? "下载 macOS 版本"
            : "下载 Windows 版本";
    };

    fetch("update.json", { cache: "no-store" })
        .then((response) => {
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            return response.json();
        })
        .then((manifest) => {
            const version = `v${manifest.version}`;
            document.querySelectorAll("[data-version]").forEach((element) => {
                element.textContent = version;
            });

            Object.entries(manifest.platforms || {}).forEach(([key, item]) => {
                const button = document.querySelector(`[data-download="${key}"]`);
                const size = document.querySelector(`[data-size="${key}"]`);
                if (button && item.url) button.href = item.url;
                if (size) size.textContent = humanSize(item.size);
            });

            setRecommendedDownload(manifest.platforms?.[recommendedPlatform]?.url);

            const notes = document.querySelector("[data-release-notes]");
            if (notes && manifest.releaseNotes) {
                notes.textContent = manifest.releaseNotes;
            }

            const publishedAt = document.querySelector("[data-published-at]");
            const date = new Date(manifest.publishedAt);
            if (publishedAt && !Number.isNaN(date.getTime())) {
                publishedAt.dateTime = date.toISOString();
                publishedAt.textContent = `${new Intl.DateTimeFormat("zh-CN", {
                    year: "numeric",
                    month: "2-digit",
                    day: "2-digit"
                }).format(date)} 发布`;
            }
        })
        .catch(() => {
            setRecommendedDownload(
                document.querySelector(
                    `[data-download="${recommendedPlatform}"]`
                )?.href
            );
        });
})();
