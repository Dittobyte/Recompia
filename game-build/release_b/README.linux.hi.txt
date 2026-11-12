# source-sha256: dfe69f0a7815483099f203214ec0ee4eb78a925447fe65fef5ef14c2d3360eee
यह अंग्रेज़ी README का मशीनी अनुवाद है, इसमें गलतियाँ हो सकती हैं, और अंग्रेज़ी README ही आधिकारिक है।

Pokopia PC @VERSION@ Linux, SteamOS और Steam Deck के लिए

1. glibc 2.38 या नए वर्ज़न वाले सिस्टम पर (Ubuntu 24.04, Fedora 39, Bazzite और उसके बाद के) गेम एक नेटिव Linux प्रोग्राम के रूप में चलता है और उसे Proton की ज़रूरत नहीं होती। पुराने सिस्टम पर (Ubuntu 22.04, Debian 12, Mint 21) पहले Steam में Proton Experimental इंस्टॉल करें (Library, Tools; Steam Deck पर पहले Desktop Mode में जाएँ)।
2. PokopiaLauncher.AppImage चलाएँ और अपना डंप फ़ोल्डर चुनें (base/ और updates/<version>/, दोनों में exefs/ और romfs/) या कोई .nup. वर्ज़न 2.0.1a ज़रूरी है।
3. Set up दबाएँ। पहली बार यह अपने बिल्ड टूल डाउनलोड करता है (Python और Zig, लगभग 100 MB), फिर इस PC पर गेम और शेडर कैश बनाता है: नेटिव गेम, या पुराने सिस्टम पर Proton के ज़रिए pkp.exe। आप रोककर बाद में जारी रख सकते हैं।
4. Play दबाएँ। गेम सीधे शुरू होता है, या पुराने सिस्टम पर Steam के Proton के ज़रिए।

अगर PokopiaLauncher.AppImage शुरू नहीं होता (FUSE के बिना सिस्टम): ./PokopiaLauncher.AppImage --appimage-extract-and-run
Steam से (Deck पर Game Mode): PokopiaLauncher.AppImage को non-Steam गेम के रूप में जोड़ें और लॉन्च विकल्प --play सेट करें।

अपडेट: नई रिलीज़ को अपने इंस्टॉल पर एक्सट्रैक्ट करें। अगर अपडेट गेम का कोड बदलता है, तो लॉन्चर बताता है: फिर Set up दबाएँ और यह गेम दोबारा बनाता है। नेटिव गेम वाली पहली रिलीज़ glibc 2.38 या नए वर्ज़न पर एक बार फिर Set up माँगती है। आपके शेडर और सेव बचे रहते हैं।

समुदाय और सहायता: https://discord.recompia.com

लाइसेंस: LICENSE.txt और licenses/.
