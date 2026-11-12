# source-sha256: dfe69f0a7815483099f203214ec0ee4eb78a925447fe65fef5ef14c2d3360eee
هذه ترجمة آلية لملف README الإنجليزي، وقد تحتوي على أخطاء. النسخة الإنجليزية هي المرجع.

Pokopia PC @VERSION@ لنظام Linux وSteamOS وSteam Deck

1. على نظام فيه glibc 2.38 أو أحدث (Ubuntu 24.04 وFedora 39 وBazzite وما بعدها) تعمل اللعبة كبرنامج Linux أصلي ولا تحتاج إلى Proton. على نظام أقدم (Ubuntu 22.04 وDebian 12 وMint 21) ثبّت أولاً Proton Experimental في Steam (المكتبة، الأدوات؛ على Steam Deck بدّل إلى وضع سطح المكتب أولاً).
2. شغّل PokopiaLauncher.AppImage واختر مجلد نسختك المستخرجة (base/ و updates/<version>/، ولكل منهما exefs/ و romfs/) أو ملف .nup. الإصدار 2.0.1a مطلوب.
3. اضغط Set up. في المرة الأولى ينزّل أدوات البناء (Python وZig، نحو 100 ميغابايت)، ثم يبني اللعبة وذاكرة الشيدرات على هذا الحاسوب: اللعبة الأصلية، أو pkp.exe عبر Proton على نظام أقدم. يمكنك الإيقاف المؤقت والمتابعة لاحقاً.
4. اضغط Play. تبدأ اللعبة مباشرة، أو عبر Proton من Steam على نظام أقدم.

إذا لم يبدأ PokopiaLauncher.AppImage (نظام بدون FUSE): ./PokopiaLauncher.AppImage --appimage-extract-and-run
من Steam (وضع الألعاب على Deck): أضف PokopiaLauncher.AppImage كلعبة غير تابعة لـ Steam مع خيارات التشغيل --play.

التحديث: استخرج الإصدار الجديد فوق تثبيتك. إذا غيّر التحديث شيفرة اللعبة، يخبرك المشغّل: اضغط Set up مرة أخرى وسيعيد بناء اللعبة. أول إصدار فيه اللعبة الأصلية يطلب Set up مرة أخرى مرة واحدة على glibc 2.38 أو أحدث. تبقى شيدراتك وملفات حفظك.

المجتمع والمساعدة: https://discord.recompia.com

التراخيص: LICENSE.txt و licenses/.
