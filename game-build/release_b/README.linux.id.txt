# source-sha256: c961cc3a5b624b2c6b6ad45ae7a81f0a4f866bf8e4e209c61d8f914245420cdf
Ini adalah terjemahan bahasa Indonesia dari teks bahasa Inggris. Jika ada perbedaan, teks bahasa Inggris adalah rujukan resmi.

Recompia @VERSION@ untuk Linux, SteamOS, dan Steam Deck

1. Anda memerlukan driver Vulkan: driver Mesa untuk AMD dan Intel atau driver proprietary NVIDIA. Tidak perlu hal lain, karena game, runtime, dan penerjemah shader adalah biner native Linux.
2. Jalankan @LAUNCHER@ dan pilih folder dump Anda (base/ dan updates/<version>/, masing-masing berisi exefs/ dan romfs/) atau paket .nup maupun .nx. Versi 2.0.1a wajib digunakan.
3. Tekan Set up. Pada kali pertama, alat build akan diunduh (Python dan Zig, sekitar 100 MB), lalu game dikompilasi dan shadernya diterjemahkan di PC ini. Anda dapat menjeda lalu melanjutkannya nanti.
4. Tekan Play. Game berjalan secara native: tanpa Proton, tanpa Wine. Upscaler FSR 3 dan DLSS hanya tersedia di Windows.

Jika @LAUNCHER@ tidak bisa berjalan (sistem tanpa FUSE): ./@LAUNCHER@ --appimage-extract-and-run
Dari Steam (Game Mode di Deck): tambahkan launcher melalui Settings, Add to Steam, atau tambahkan @LAUNCHER@ sebagai non-Steam game tanpa launch options. Pintasan ini membuka launcher yang memeriksa pembaruan, lalu tekan Play.

Memperbarui: ekstrak rilis baru menimpa instalasi Anda. Jika pembaruan mengubah kode game, launcher akan memberi tahu: tekan Set up again untuk membangun ulang pkp. Shader dan save Anda tetap aman.

Komunitas dan bantuan: https://discord.gg/VyRcUUQNKH

Lisensi: LICENSE.txt dan licenses/.
