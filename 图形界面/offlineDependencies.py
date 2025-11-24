import os
import requests

# 配置依赖库的下载地址 (使用稳定版本)
LIBRARIES = {
    "react.js": "https://unpkg.com/react@18/umd/react.development.js",
    "react-dom.js": "https://unpkg.com/react-dom@18/umd/react-dom.development.js",
    "babel.js": "https://unpkg.com/@babel/standalone/babel.min.js",
    "tailwind.js": "https://cdn.tailwindcss.com"  # 下载 Standalone 版本
}


def download_libs():
    # 创建 libs 目录
    if not os.path.exists("libs"):
        os.makedirs("libs")
        print("Created directory: libs/")

    print("Starting download of dependencies...")

    for filename, url in LIBRARIES.items():
        filepath = os.path.join("libs", filename)
        if os.path.exists(filepath):
            print(f"{filename} already exists. Skipping.")
            continue

        print(f"Downloading {filename}...")
        try:
            response = requests.get(url)
            if response.status_code == 200:
                with open(filepath, "wb") as f:
                    f.write(response.content)
                print(f"Saved {filename}")
            else:
                print(f"Failed to download {filename}: Status {response.status_code}")
        except Exception as e:
            print(f"Error downloading {filename}: {e}")

    print("\nDependencies ready! You can now open 'index.html' offline.")


if __name__ == "__main__":
    download_libs()
