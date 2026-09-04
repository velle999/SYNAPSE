# हिन्दी (hi) — syn-install's own words.
#
# ⚠ THE KEY IS THE ENGLISH SENTENCE. A translation whose key matches nothing in
# syn-install.sh is dead text that will never appear again, and it is the only
# evidence that an English string was edited — tests/i18n_test.sh fails on one.
#
# An entry left out prints the English. That is not a failure state: an
# installer that says one screen in English is usable, and one that says a
# screen wrong is not.
#
# ⚠ THE ANSWER KEYS STAY [Y/n], [y/N] AND 'yes'. The prompts around them are
# translated; the letters are not, because the script compares against y/n and
# the literal word yes. A translated key would be a question whose own answer
# does not work.
#
# Edit by hand, or with tools/i18n-fill.py, which matches on the English rather
# than on a line number.
#
# SPDX-License-Identifier: GPL-2.0-or-later

declare -gA SYN_T=(
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix नहीं मिला — इस मशीन पर 'syn' पैकेज स्थापित नहीं है।"
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen विफल रहा। लाइव सत्र अंग्रेज़ी में ही रहेगा; स्थापना पर इसका
  असर नहीं पड़ता, क्योंकि वह लोकेल लक्ष्य तंत्र के भीतर बनाती है।"
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  कीबोर्ड, घड़ी, फ़ॉन्ट और शेल — सब इसी चुनाव के अनुसार चलेंगे।
  यह सब बाद में बदला जा सकता है।"
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="बदलें [संख्याएँ, 'all', 'none', Enter = स्वीकार]:"
  ["--config needs a file"]="--config के लिए एक फ़ाइल चाहिए"
  ["syn-install must be run as root"]="syn-install को root के रूप में चलाना होगा"
  ["  SynapseOS is running from the live image."]="  SynapseOS लाइव इमेज से चल रहा है।"
  ["Starting the desktop — the installer opens with it."]="डेस्कटॉप शुरू हो रहा है — इंस्टॉलर उसी के साथ खुलेगा।"
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  यह इंस्टॉलर ये काम करेगा:
    1. डिस्क का विभाजन
    2. SynapseOS का आधार तंत्र स्थापित करना
    3. SynapseOS के पैकेज स्थापित करना
    4. उपयोक्ता खाता बनाना
    5. डेस्कटॉप वातावरण चुनना
    6. तंत्र और बूटलोडर की व्यवस्था करना"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="लक्ष्य डिस्क का सारा डेटा मिटा दिया जाएगा"
  ["Press ENTER to continue or Ctrl+C to abort..."]="आगे बढ़ने के लिए ENTER दबाएँ, या रोकने के लिए Ctrl+C..."
  ["Checking network"]="नेटवर्क जाँचा जा रहा है"
  ["Network is up"]="नेटवर्क चालू है"
  ["  No network detected. Starting NetworkManager..."]="  कोई नेटवर्क नहीं मिला। NetworkManager शुरू किया जा रहा है..."
  ["  No connection — but this machine has Wi-Fi."]="  कोई संपर्क नहीं — पर इस मशीन में Wi-Fi है।"
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Wi-Fi चुनने का परदा (nmtui) खोलें? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="न नेटवर्क संपर्क है, न कोई Wi-Fi उपकरण जिसे सेट किया जा सके।
  SynapseOS स्थापना के दौरान आधार तंत्र उतारता है, इसलिए तार जोड़कर
  दोबारा चलाइए।"
  ["Network connected"]="नेटवर्क जुड़ गया"
  ["Step 1 — Select Target Disk"]="चरण 1 — लक्ष्य डिस्क चुनें"
  ["  Available disks:"]="  उपलब्ध डिस्क:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="लक्ष्य डिस्क (जैसे sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="लक्ष्य डिस्क इस्तेमाल में है। उसके विभाजन अनमाउंट कर के दोबारा चलाइए।"
  ["Boot mode: UEFI"]="बूट विधि: UEFI"
  ["Boot mode: BIOS/Legacy"]="बूट विधि: BIOS/लीगेसी"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  मूल फ़ाइल तंत्र को LUKS2 से गुप्त करता है। हर बार बूट पर, तंत्र शुरू
  होने से पहले पासफ़्रेज़ पूछा जाएगा।"
  ["Encrypt the disk? [y/N]:"]="डिस्क को गुप्त करें? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          गोपन के साथ यही बेहतर चुनाव है: कर्नेल EFI
                          विभाजन पर रहता है और खोलता सिर्फ़ initramfs
                          है, इसलिए /boot के लिए अलग बिना-गोपन विभाजन
                          की ज़रूरत नहीं पड़ती।"
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          यह हर स्नैपशॉट का कर्नेल EFI विभाजन पर
                          उतारता है, इसलिए स्नैपशॉट चालू होने पर वह
                          विभाजन कहीं बड़ा बनाया जाता है।"
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  स्नैपशॉट सस्ते हैं, मुफ़्त नहीं: जो कुछ बदलता है उसकी पुरानी नकल
  रखते हैं, इसलिए लगभग भरी डिस्क लगभग भरी ही रहती है।"
  ["Enable snapshots? [Y/n]:"]="स्नैपशॉट चालू करें? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="इस इंस्टॉलर इमेज में mkfs.ext4 नहीं है — /boot नहीं बनाया जा सकता"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="इस इंस्टॉलर इमेज में btrfs नहीं है — सबवॉल्यूम नहीं बनाए जा सकते"
  ["Are these correct? [Y/n]:"]="क्या यह सही है? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="सवाल फिर से शुरू — डिस्क को छुआ तक नहीं गया है।"
  ["cryptsetup is not available on this installer image"]="इस इंस्टॉलर इमेज पर cryptsetup उपलब्ध नहीं है"
  ["Encryption passphrase:"]="गोपन का पासफ़्रेज़:"
  ["Repeat passphrase:"]="पासफ़्रेज़ दोहराइए:"
  ["Empty passphrase — that would leave the disk unprotected."]="पासफ़्रेज़ खाली है — इससे डिस्क असुरक्षित रह जाएगी।"
  ["Passphrases did not match — try again."]="पासफ़्रेज़ मेल नहीं खाए — फिर से कोशिश कीजिए।"
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="पासफ़्रेज़ 8 अक्षरों से छोटा है। जिसके हाथ में डिस्क हो, उसके सामने
  छोटा पासफ़्रेज़ ज़्यादा काम का नहीं।"
  ["Use it anyway? [y/N]:"]="फिर भी इस्तेमाल करें? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="गोपन चालू — मूल विभाजन LUKS2 होगा"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open विफल — पासफ़्रेज़ स्वीकार नहीं हुआ"
  ["Failed to mount root"]="मूल विभाजन माउंट नहीं हो सका"
  ["  Creating btrfs subvolumes..."]="  btrfs सबवॉल्यूम बनाए जा रहे हैं..."
  ["btrfs: could not create @"]="btrfs: @ नहीं बनाया जा सका"
  ["btrfs: could not create @home"]="btrfs: @home नहीं बनाया जा सका"
  ["btrfs: could not create @snapshots"]="btrfs: @snapshots नहीं बनाया जा सका"
  ["btrfs: could not create @var_log"]="btrfs: @var_log नहीं बनाया जा सका"
  ["btrfs: could not create @pkg"]="btrfs: @pkg नहीं बनाया जा सका"
  ["could not remount the btrfs root onto @"]="btrfs मूल को @ पर दोबारा माउंट नहीं किया जा सका"
  ["Failed to mount @"]="@ माउंट नहीं हो सका"
  ["Failed to mount @home"]="@home माउंट नहीं हो सका"
  ["Failed to mount @snapshots"]="@snapshots माउंट नहीं हो सका"
  ["Failed to mount @var_log"]="@var_log माउंट नहीं हो सका"
  ["Failed to mount @pkg"]="@pkg माउंट नहीं हो सका"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="इससे खाली जगह में एक विभाजन जुड़ेगा। जो चीज़ें अपूरणीय हैं, उनका बैकअप पहले ले लीजिए।"
  ["Type 'yes' to install alongside:"]="साथ में स्थापित करने के लिए 'yes' लिखिए:"
  ["Aborted"]="रोक दिया गया"
  ["Failed to create the root partition"]="मूल विभाजन नहीं बनाया जा सका"
  ["Could not identify the new partition after creating it"]="बनाने के बाद नया विभाजन पहचाना नहीं जा सका"
  ["Failed to format root partition"]="मूल विभाजन फ़ॉर्मैट नहीं हो सका"
  ["Failed to mount the existing ESP"]="मौजूदा ESP माउंट नहीं हो सका"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="इस इमेज में कोई विभाजन संपादक नहीं है (cfdisk, fdisk और parted — तीनों नहीं हैं)"
  ["  What this install needs:"]="  इस स्थापना को क्या चाहिए:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • एक EFI तंत्र विभाजन (प्रकार EF00 / 'esp' फ़्लैग) — मौजूदा को दोबारा इस्तेमाल किया जा सकता है"
  ["  Skipping the partition editor (--config)."]="  विभाजन संपादक छोड़ा जा रहा है (--config)।"
  ["Format it? Everything on it is lost [y/N]:"]="फ़ॉर्मैट करें? उस पर जो कुछ है, सब चला जाएगा [y/N]:"
  ["Separate /boot partition:"]="अलग /boot विभाजन:"
  ["Swap partition (blank for none):"]="स्वैप विभाजन (कोई नहीं तो खाली छोड़ें):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="दोबारा बनाएँ? इसका UUID बदल जाएगा और उस तंत्र की fstab टूट जाएगी [y/N]:"
  ["Type 'yes' to format these:"]="इन्हें फ़ॉर्मैट करने के लिए 'yes' लिखिए:"
  ["  Formatting EFI partition..."]="  EFI विभाजन फ़ॉर्मैट किया जा रहा है..."
  ["  Formatting /boot partition..."]="  /boot विभाजन फ़ॉर्मैट किया जा रहा है..."
  ["Failed to mount /boot"]="/boot माउंट नहीं हो सका"
  ["Type 'yes' to confirm:"]="पुष्टि के लिए 'yes' लिखिए:"
  ["  Creating GPT partition table..."]="  GPT विभाजन तालिका बनाई जा रही है..."
  ["Failed to format EFI partition"]="EFI विभाजन फ़ॉर्मैट नहीं हो सका"
  ["Failed to format boot partition"]="बूट विभाजन फ़ॉर्मैट नहीं हो सका"
  ["  Creating MBR partition table..."]="  MBR विभाजन तालिका बनाई जा रही है..."
  ["Disk partitioned and mounted at /mnt"]="डिस्क विभाजित हुई और /mnt पर माउंट हो गई"
  ["Step 3 — Installing Base System"]="चरण 3 — आधार तंत्र की स्थापना"
  ["  Initializing pacman keyring..."]="  pacman की कुंजी-शृंखला तैयार की जा रही है..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap चल रहा है (इसमें कई मिनट लग सकते हैं)..."
  ["pacstrap failed — check network connection"]="pacstrap विफल — नेटवर्क संपर्क जाँचिए"
  ["grub-install not found in chroot — attempting recovery..."]="chroot में grub-install नहीं मिला — सुधार की कोशिश की जा रही है..."
  ["Could not install grub into target — check network"]="लक्ष्य में grub स्थापित नहीं किया जा सका — नेटवर्क जाँचिए"
  ["Base system installed"]="आधार तंत्र स्थापित हो गया"
  ["Step 4 — Choose What to Install"]="चरण 4 — चुनिए कि क्या स्थापित हो"
  ["  What should be installed alongside the SynapseOS core?"]="  SynapseOS के मूल के साथ और क्या स्थापित किया जाए?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   ब्लूटूथ, छपाई, Wine, फ़ोन   (मूल रूप से)"
  ["                   the ordinary software people install anyway"]="                   वही आम सॉफ़्टवेयर जो लोग वैसे भी लगाते हैं"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  न्यूनतम को छोड़कर हर तैयार सेट आगे पूछता है कि कौन-सा AI मॉडल
  उतारना है, और उसे छोड़ देना भी एक उत्तर है।"
  ["Full install selected"]="पूर्ण स्थापना चुनी गई"
  ["Minimal install selected"]="न्यूनतम स्थापना चुनी गई"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  सवाल दो तरह के हैं। पहले पैकेज, चेकबॉक्स के पन्नों के रूप में;
  फिर वे थोड़े से विकल्प जो पैकेज नहीं बल्कि एक पूरा उपतंत्र हैं।"
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  और वही सॉफ़्टवेयर जो लोग पहली ही शाम को वैसे भी लगाते हैं।
  यह सब Arch के भंडारों में है; इनमें से कुछ भी हमारा नहीं।"
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  बाकी हाँ/नहीं (y/n) है। मूल विकल्प (बड़े अक्षरों में) मानक है।"
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update हटा दिया गया: इस मशीन के पास अगला SynapseOS पैकेज पाने का
  कोई रास्ता नहीं बचेगा। बाद में ठीक करने का मतलब है उसे ISO से हाथ से
  लगाना, या फिर से स्थापना करना।"
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="न डेस्कटॉप रखा गया, न AI डीमन। वह कुछ SynapseOS औज़ारों वाला एक Arch
  तंत्र होगा, जो एक स्वीकार्य उत्तर है — पर दस्तावेज़ों में कहीं भी
  उस मशीन का वर्णन नहीं मिलेगा जो इससे बनती है।"
  ["Custom install configured"]="अपनी पसंद की स्थापना तय हो गई"
  ["Standard install selected"]="मानक स्थापना चुनी गई"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd एक मॉडल चढ़ाता है और SynapseOS में AI से जुड़ी हर चीज़ उसी से
  बात करती है: synsh, डेस्कटॉप का AI पैनल, Chibi, Vibe। वह अभी, इसी
  संपर्क से, उस डिस्क पर उतारा जाएगा जिस पर आप स्थापना कर रहे हैं।"
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  छोटा मॉडल सिर्फ़ तेज़ और हल्का नहीं होता: वह निर्देशों का पालन भी कम
  करता है। synsh आपकी बात गलत समझता है, Vibe के कोड में ज़्यादा सुधार
  लगते हैं, Chibi बात का सिरा खो देता है। जब तक डिस्क या स्मृति मना न
  करे, मूल विकल्प ही लीजिए — 7B को लगभग 6 GB खाली RAM चाहिए।"
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  आप जो भी चुनें, बाद में बदला जा सकता है: 'syn model download',
  या डेस्कटॉप पर Super+C ▸ तंत्र ▸ AI मॉडल।"
  ["Install this selection? [Y/n]:"]="यही चयन स्थापित करें? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="फिर से चुनते हैं — अभी तक कुछ भी स्थापित नहीं हुआ है।"
  ["Step 4b — Installing SynapseOS"]="चरण 4b — SynapseOS की स्थापना"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="/etc/pacman.conf में ILoveCandy चालू नहीं किया जा सका (सिर्फ़ दिखावटी बात है)।"
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  [multilib] चालू किया जा रहा है (32-बिट भंडार, Steam को चाहिए)..."
  ["Could not sync the multilib database — Steam may fail to install"]="multilib का डेटाबेस समकालिक नहीं हो सका — Steam शायद स्थापित न हो"
  ["Could not enable [multilib]; Steam will be skipped."]="[multilib] चालू नहीं हो सका; Steam छोड़ दिया जाएगा।"
  ["Some SynapseOS packages failed to install — verifying below"]="कुछ SynapseOS पैकेज स्थापित नहीं हुए — नीचे जाँच हो रही है"
  ["No SynapseOS packages were selected. This will be an Arch system."]="कोई SynapseOS पैकेज नहीं चुना गया। यह एक Arch तंत्र होगा।"
  ["SynapseOS packages installed"]="SynapseOS के पैकेज स्थापित हो गए"
  ["Component selection recorded in /etc/synapseos/components.conf"]="घटकों का चयन /etc/synapseos/components.conf में दर्ज कर दिया गया"
  ["Step 5 — Create User Account"]="चरण 5 — उपयोक्ता खाता बनाएँ"
  ["  Create a user account for the installed system."]="  स्थापित तंत्र के लिए एक उपयोक्ता खाता बनाइए।"
  ["Username [default: syn]:"]="उपयोक्ता नाम [मूल रूप से: syn]:"
  ["Full name (optional):"]="पूरा नाम (ऐच्छिक):"
  ["Password:"]="कूटशब्द:"
  ["Confirm password:"]="कूटशब्द की पुष्टि:"
  ["Passwords do not match or are empty — try again"]="कूटशब्द मेल नहीं खाते या खाली हैं — फिर से कोशिश कीजिए"
  ["Step 6 — Desktop Environment"]="चरण 6 — डेस्कटॉप वातावरण"
  ["  Choose a desktop environment:"]="  एक डेस्कटॉप वातावरण चुनिए:"
  ["  Installing KDE Plasma..."]="  KDE Plasma स्थापित किया जा रहा है..."
  ["Some KDE packages failed to install"]="कुछ KDE पैकेज स्थापित नहीं हुए"
  ["KDE Plasma installed"]="KDE Plasma स्थापित हो गया"
  ["  Installing GNOME..."]="  GNOME स्थापित किया जा रहा है..."
  ["Some GNOME packages failed to install"]="कुछ GNOME पैकेज स्थापित नहीं हुए"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME स्थापित हो गया (सिर्फ़ सत्र — अनुप्रयोग SynapseOS के हैं, GNOME के नहीं)"
  ["  Installing greetd (login screen) + desktop extras..."]="  greetd (लॉगिन परदा) और डेस्कटॉप की अतिरिक्त चीज़ें स्थापित की जा रही हैं..."
  ["greetd failed to install — boot falls back to getty login"]="greetd स्थापित नहीं हुआ — बूट getty लॉगिन पर लौट आएगा"
  ["SynapseUI selected (included)"]="SynapseUI चुना गया (साथ ही आता है)"
  ["Installing Wine"]="Wine स्थापित किया जा रहा है"
  ["Wine installed"]="Wine स्थापित हो गया"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine स्थापित नहीं हुआ — Windows की .exe/.msi फ़ाइलें नहीं चलेंगी।
  बाद में 'sudo pacman -S wine wine-mono' से लगाइए।"
  ["Configuring Video Driver"]="वीडियो ड्राइवर की व्यवस्था"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  आभासी मशीन — mesa स्थापित किया जा रहा है (यहाँ synui pixman इस्तेमाल करता है)..."
  ["mesa failed to install"]="mesa स्थापित नहीं हुआ"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="NVIDIA ड्राइवर की स्थापना विफल — इस हालत में तंत्र nouveau पर बूट होगा
  और synui का रेंडरर कभी शुरू ही नहीं होगा"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="NVIDIA की निद्रा-सेवाएँ चालू (VRAM सहेजना/लौटाना)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="nvidia-{suspend,resume,hibernate} चालू नहीं हो सकीं — अगर बाद में
  NVreg_PreserveVideoMemoryAllocations चालू किया गया तो निद्रा से लौटने पर परदा काला रह सकता है"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd इस GPU पर अनुमान लगा सकता है, CPU के बजाय।
  इसके लिए CUDA का वातावरण उतारा जाएगा (स्थापना के बाद लगभग 4.7 GiB)।"
  ["Enable GPU inference? [Y/n]:"]="GPU पर अनुमान चालू करें? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="CPU पर ही अनुमान रहेगा। बाद में बदलने के लिए:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  synapse-llama-cuda स्थापित किया जा रहा है (इसमें कुछ समय लगेगा)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="इस ISO में llama का GPU वाला रूप नहीं है, इसलिए NVIDIA कार्ड होते हुए भी
  synapd CPU पर चलेगा। (synapse-llama-cuda तभी बनता है जब ISO को CUDA
  टूलकिट वाली मशीन पर बनाया जाए।)"
  ["Video driver install failed — synui may fall back to software rendering"]="वीडियो ड्राइवर की स्थापना विफल — synui शायद सॉफ़्टवेयर रेंडरिंग पर उतर आए"
  ["Video drivers installed"]="वीडियो ड्राइवर स्थापित हो गए"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  GPU पर अनुमान चालू किया जा रहा है (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="इस ISO में llama का Vulkan वाला रूप नहीं है, इसलिए AMD/Intel GPU होते हुए भी
  synapd CPU पर चलेगा। (ISO को 'shaderc' और vulkan-headers वाली मशीन पर
  बनाइए, तभी synapse-llama-vulkan होगा।)"
  ["Installing Steam and the game stack"]="Steam और खेल का ढाँचा स्थापित किया जा रहा है"
  ["  Installing steam and the 32-bit runtime libraries..."]="  steam और 32-बिट रनटाइम लाइब्रेरियाँ स्थापित की जा रही हैं..."
  ["Steam installed (native multilib package)"]="Steam स्थापित हो गया (मूल multilib पैकेज)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam स्थापित नहीं हुआ। बाकी तंत्र पूरा है —
  बाद में 'sudo pacman -S steam' से लगाइए ([multilib] पहले से ही
  /etc/pacman.conf में चालू है)।"
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  खेल का ढाँचा स्थापित किया जा रहा है (ओवरले, गवर्नर, सूक्ष्म-कंपोज़िटर)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="खेल का ढाँचा स्थापित हो गया (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="खेल का ढाँचा स्थापित नहीं हुआ। Steam फिर भी चलेगा; पर FPS ओवरले,
  CPU/GPU गवर्नर और 'synui-game-run --gamescope' नहीं चलेंगे।
  बाद में लगाइए:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="CachyOS Proton स्थापित किया जा रहा है"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  CachyOS की कुंजी-शृंखला और मिरर सूची लाई जा रही है..."
  ["  Trusting the CachyOS master key..."]="  CachyOS की मुख्य कुंजी पर भरोसा किया जा रहा है..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="keyserver.ubuntu.com से CachyOS की मुख्य कुंजी नहीं मिल सकी।
  उसके बिना हस्ताक्षरित कुंजी-शृंखला नहीं लगाई जा सकती, इसलिए CachyOS
  Proton छोड़ा जा रहा है। बाद में जोड़िए:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  मुख्य कुंजी अपेक्षा के अनुरूप है — उस पर भरोसा किया जा रहा है..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] जुड़ तो गया पर उसमें कोई पैकेज नहीं है — उसे वापस हटाया
  जा रहा है ताकि आगे कोई उन्नयन न अटके।"
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="CachyOS की कुंजी-शृंखला में अपेक्षित मुख्य कुंजी नहीं है।
  भरोसा करने से इनकार — भंडार जोड़ा नहीं गया।"
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  proton-cachyos-slr स्थापित किया जा रहा है (लगभग 340 MB उतरेगा)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton स्थापित हो गया — Steam में हर खेल के लिए गुण →
  संगतता में इसे 'proton-cachyos-… (steam linux runtime)' के रूप में चुनिए।
  Steam इसे सिर्फ़ शुरू होते समय खोजता है, इसलिए चल रहा हो तो दोबारा शुरू कीजिए।"
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr स्थापित नहीं हुआ। Steam और Valve का अपना Proton
  अप्रभावित हैं। बाद में लगाइए:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="[cachyos] भंडार चालू नहीं हो सका, इसलिए CachyOS Proton छोड़ा गया।
  Steam, Valve के Proton के साथ फिर भी चलता है। बाद में जोड़ने के लिए:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="BlackArch चालू किया जा रहा है"
  ["  Fetching the BlackArch bootstrap..."]="  BlackArch का बूटस्ट्रैप लाया जा रहा है..."
  ["  Master key pinned as expected — running bootstrap..."]="  मुख्य कुंजी अपेक्षा के अनुरूप है — बूटस्ट्रैप चलाया जा रहा है..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring स्थापित नहीं हुआ — कुंजियों के बदलाव इस मशीन तक
  नहीं पहुँचेंगे। 'sudo pacman -S blackarch-keyring' से ठीक कीजिए।"
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="उतारी गई strap.sh, BlackArch की अपेक्षित मुख्य कुंजी तय नहीं करती।
  उसे चलाने से इनकार — भंडार जोड़ा नहीं गया।"
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch चालू नहीं हुआ। बाकी तंत्र पूरा है;
  बाद में 'sudo syn arsenal --enable-repo' से जोड़िए।"
  ["Installing software"]="सॉफ़्टवेयर स्थापित किया जा रहा है"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="वह कार्रवाई विफल रही — अब हर पैकेज अलग-अलग आज़माया जा रहा है, ताकि जो
  ठीक हैं वे फिर भी आ जाएँ और जो ठीक नहीं उसका नाम सामने आए।"
  ["Software installed"]="सॉफ़्टवेयर स्थापित हो गया"
  ["Installing Flatpak apps"]="Flatpak अनुप्रयोग स्थापित किए जा रहे हैं"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak स्थापित नहीं हो सका — Flatpak अनुप्रयोग छोड़े जा रहे हैं।
  और किसी चीज़ पर असर नहीं।"
  ["Could not add the flathub remote"]="flathub रिमोट नहीं जोड़ा जा सका"
  ["Flatpak apps installed"]="Flatpak अनुप्रयोग स्थापित हो गए"
  ["Configuring System"]="तंत्र की व्यवस्था"
  ["  fstab generated"]="  fstab बन गई"
  ["Swap recorded in fstab"]="स्वैप fstab में दर्ज हो गया"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram तय हो गया (संपीड़ित स्वैप, RAM का आधा, अधिकतम 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="लक्ष्य में zram-generator स्थापित नहीं है — संपीड़ित स्वैप नहीं होगा"
  ["  Hostname: synapse"]="  मेज़बान नाम: synapse"
  ["Step 7 — Language & Region"]="चरण 7 — भाषा और क्षेत्र"
  ["   0) Other — enter a locale by hand"]="   0) अन्य — लोकेल हाथ से लिखिए"
  ["Locale (e.g. sv_SE.UTF-8):"]="लोकेल (जैसे sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="कंसोल कीमैप (जैसे sv-latin1):"
  ["Step 8 — Timezone"]="चरण 8 — समय क्षेत्र"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) अन्य — tzdata का कोई भी नाम लिखिए (जैसे Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata नाम (क्षेत्र/शहर):"
  ["  Did you mean:"]="  क्या आपका मतलब यह था:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  सूची में से एक संख्या चुनिए, या देखिए: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: लाइव तंत्र से नकल किया गया"
  ["  issue: copied from live system"]="  issue: लाइव तंत्र से नकल किया गया"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="लक्ष्य फ़ाइल तंत्र अब लिखने योग्य नहीं रहा (डिस्क की गड़बड़ी? 'dmesg' देखिए) — रोका जा रहा है"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="ड्रॉप-इन लिखने के बाद sudoers के नियम अवैध हैं — ऐसा तंत्र नहीं सौंपा जाएगा जो sudo न कर सके"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="/etc/pam.d/system-auth में pam_faillock को ढीला नहीं किया जा सका (बिना टर्मिनल वाला sudo अब भी खाते को रीबूट तक बंद कर सकता है)।"
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="/var/lib/synapse-src पहले से नहीं बनाया जा सका — अद्यतन औज़ार पहली बार चलने पर कूटशब्द माँगेगा"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  डेस्कटॉप: KDE Plasma (SDDM लॉगिन परदा)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: लॉगिन परदे पर SynapseOS का चिह्न"
  ["  Desktop: GNOME (GDM login screen)"]="  डेस्कटॉप: GNOME (GDM लॉगिन परदा)"
  ["  Desktop: TTY only"]="  डेस्कटॉप: सिर्फ़ TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  डेस्कटॉप: SynapseUI (synui स्वागतकर्ता — लॉगिन परदा ताला-परदे जैसा ही)"
  ["  motd: written for this installation"]="  motd: इस स्थापना के लिए लिखा गया"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  ध्यान दें: syn-rgb.path स्थापित नहीं है; RGB रोशनियाँ बंद रहेंगी"
  ["AI model"]="AI मॉडल"
  ["  AI model skipped — install one later with: syn model download"]="  AI मॉडल छोड़ा गया — बाद में एक लगाइए: syn model download"
  ["AI model installed"]="AI मॉडल स्थापित हो गया"
  ["  the install, and everything else on the disk is already done."]="  स्थापना का सबसे लंबा हिस्सा, और डिस्क पर बाकी सब पहले ही हो चुका है।"
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="लक्ष्य में syn-model नहीं है, इसलिए कोई मॉडल नहीं उतारा गया।
  वह मूल सेट का हिस्सा है; अगर उसे हटाया गया था तो AI निष्क्रिय ही रहेगा।"
  ["Configuring Nix"]="Nix की व्यवस्था"
  ["Nix configured — /etc/synapseos/nix"]="Nix तय हो गया — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  यही वह डाउनलोड है — home.nix में कोई पैकेज जोड़ने से पहले ही
  कुछ सौ MB। 'syn nix edit' उसे खोलता है।"
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix स्थापित हो गया, पर लक्ष्य में 'syn' पैकेज नहीं है, इसलिए
  विन्यासक तैयार नहीं हुआ। Nix अपने आप में चलता है;
  /etc/synapseos/nix वाली परत को 'syn' चाहिए।"
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix स्थापित नहीं हुआ — घोषणात्मक परत उपलब्ध नहीं है।
  बाद में 'sudo pacman -S nix && sudo syn nix init' से लगाइए।"
  ["  Generating initramfs..."]="  initramfs बनाया जा रहा है..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio विफल — स्थापित तंत्र बूट नहीं होगा"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="mkinitcpio के बाद initramfs नहीं है — स्थापित तंत्र बूट नहीं होगा"
  ["System configured"]="तंत्र की व्यवस्था हो गई"
  ["Installing Bootloader"]="बूटलोडर स्थापित किया जा रहा है"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) विफल"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) विफल"
  ["  Generating GRUB config..."]="  GRUB का विन्यास बनाया जा रहा है..."
  ["grub-mkconfig failed"]="grub-mkconfig विफल"
  ["grub.cfg missing after install"]="स्थापना के बाद grub.cfg नहीं है"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg में GRUB का कूटशब्द है — उसे सिर्फ़ root के पढ़ने योग्य रखा गया, इसलिए सेटिंग्स अनुप्रयोग बूट प्रविष्टियों के बारे में कुछ नहीं बता सकता"
  ["  Installing systemd-boot..."]="  systemd-boot स्थापित किया जा रहा है..."
  ["bootctl install failed"]="bootctl install विफल"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot को फ़र्मवेयर में दर्ज किया जा रहा है..."
  ["efibootmgr entry not created — the removable-media path still applies"]="efibootmgr की प्रविष्टि नहीं बनी — हटाने योग्य मीडिया वाला रास्ता ही लागू रहेगा"
  ["could not read the root filesystem UUID"]="मूल फ़ाइल तंत्र का UUID नहीं पढ़ा जा सका"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="ESP पर vmlinuz-linux नहीं है — systemd-boot को बूट करने लायक कुछ नहीं मिलेगा"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="ESP पर initramfs नहीं है — systemd-boot को बूट करने लायक कुछ नहीं मिलेगा"
  ["systemd-boot did not install its EFI binary"]="systemd-boot ने अपनी EFI फ़ाइल स्थापित नहीं की"
  ["  Installing limine..."]="  limine स्थापित किया जा रहा है..."
  ["could not copy limine's EFI binary to the ESP"]="limine की EFI फ़ाइल ESP पर नकल नहीं की जा सकी"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook स्थापित नहीं — बाद में लगाए गए कर्नेल की कोई बूट प्रविष्टि नहीं बनेगी"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="ESP पर vmlinuz-linux नहीं है — limine को बूट करने लायक कुछ नहीं मिलेगा"
  ["limine's EFI binary is not on the ESP"]="ESP पर limine की EFI फ़ाइल नहीं है"
  ["limine.conf has no kernel entry"]="limine.conf में कर्नेल की कोई प्रविष्टि नहीं है"
  ["  Verifying the encrypted boot path..."]="  गुप्त बूट-मार्ग जाँचा जा रहा है..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot अलग माउंट नहीं है — गुप्त मूल के लिए बिना-गोपन /boot चाहिए"
  ["/boot is missing from fstab — it would not be mounted after boot"]="fstab में /boot नहीं है — बूट के बाद वह माउंट नहीं होगा"
  ["Encrypted boot path verified"]="गुप्त बूट-मार्ग जाँच लिया गया"
  ["Configuring snapshots"]="स्नैपशॉट की व्यवस्था"
  ["snapper's config template is missing — snapshots cannot be configured"]="snapper का विन्यास-साँचा नहीं है — स्नैपशॉट तय नहीं किए जा सकते"
  ["could not write /etc/snapper/configs/root"]="/etc/snapper/configs/root नहीं लिखा जा सका"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper को 'root' विन्यास दिखता ही नहीं — स्नैपशॉट कभी नहीं बनेंगे"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="snapper का root विन्यास ठीक नहीं किया गया — समय-समय के स्नैपशॉट डिस्क भर देंगे"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="grub-btrfsd चालू नहीं हो सका — स्नैपशॉट बूट मेन्यू में अपने आप नहीं दिखेंगे"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="स्नैपशॉट चालू (snapper + snap-pac, GRUB से बूट हो सकते हैं)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="limine-snapper-sync चालू नहीं हो सका — स्नैपशॉट अपने आप बूट मेन्यू तक नहीं पहुँचेंगे"
  ["could not take the post-install snapshot"]="स्थापना के बाद वाला स्नैपशॉट नहीं बन सका"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="पहली बूट पर स्नैपशॉट का समकालन चालू नहीं हो सका — मेन्यू पहले उन्नयन के बाद भरेगा"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="स्नैपशॉट चालू (snapper + snap-pac, limine से बूट हो सकते हैं)"
  ["Bootloader installed"]="बूटलोडर स्थापित हो गया"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  root खाता बंद है (न root से लॉगिन, न su)।
  ध्यान दें: 3 बार गलत कूटशब्द पर खाता 10 मिनट के लिए बंद हो जाता है।"
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="हर बूट पर, लॉगिन परदे से पहले, गोपन का पासफ़्रेज़ पूछा जाएगा।
  उसे वापस पाने का कोई रास्ता नहीं है।"
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    यह डिस्क गुप्त है या नहीं, और कैसे
    sudo syn-crypt change-key           पासफ़्रेज़ बदलिए
    sudo syn-crypt add-key              दूसरा जोड़िए
    sudo syn-crypt backup-header फ़ाइल  LUKS हेडर सहेजिए"
  ["  means the data is unrecoverable even with the right passphrase."]="  का मतलब है कि सही पासफ़्रेज़ होते हुए भी डेटा वापस नहीं मिलेगा।"
  ["Remove installation media and press ENTER to reboot..."]="स्थापना का माध्यम निकालिए और रीबूट के लिए ENTER दबाइए..."
  ["Install SynapseOS     — right here, in this terminal"]="SynapseOS स्थापित करें  — यहीं, इसी टर्मिनल में"
  ["Install graphically   — starts the desktop first"]="चित्रात्मक स्थापना     — पहले डेस्कटॉप शुरू करता है"
  ["Try the live desktop  — look around; install later"]="लाइव डेस्कटॉप आज़माएँ  — देख लीजिए; स्थापना बाद में"
  ["Target:"]="लक्ष्य:"
  ["ALONGSIDE"]="साथ में"
  ["ERASE"]="मिटाएँ"
  ["ADVANCED"]="उन्नत"
  ["Encrypt this installation?"]="इस स्थापना को गुप्त करें?"
  ["There is no recovery."]="वापस पाने का कोई रास्ता नहीं।"
  ["Root filesystem"]="मूल फ़ाइल तंत्र"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — मूल विकल्प। नीरस, परखा हुआ, किसी भी औज़ार से ठीक होने वाला।"
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — स्नैपशॉट और zstd संपीड़न। खराब अद्यतन को बूट मेन्यू से
                    वापस लौटाइए। RAM और CPU ज़्यादा लगते हैं।"
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — बड़ी फ़ाइलों पर तेज़। स्नैपशॉट नहीं, और एक बार बना देने पर
                    छोटा नहीं किया जा सकता।"
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — फ़्लैश के लिए बना। SD कार्ड और सस्ते SSD पर अच्छा;
                    इतना कम प्रचलित कि गिने-चुने बचाव-औज़ार ही इसे जानते हैं।"
  ["Bootloader"]="बूटलोडर"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — मूल विकल्प। दूसरे तंत्र पहचानता है, और यहाँ अकेला
                          वही है जो btrfs का स्नैपशॉट बूट कर सकता
                          है।"
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — न्यूनतम। न तंत्र पहचानता है, न स्नैपशॉट का मेन्यू।"
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — नया और तेज़, और यह स्नैपशॉट बूट कर सकता है।"
  ["Automatic snapshots?"]="स्नैपशॉट अपने आप बनें?"
  ["Review the plan — nothing has been written yet:"]="योजना देख लीजिए — अभी तक कुछ भी लिखा नहीं गया है:"
  ["nothing else is touched"]="और कुछ नहीं छुआ जाएगा"
  ["not"]="नहीं"
  ["Partition"]="विभाजन कीजिए"
  ["now."]="अभी।"
  ["Partitions now on"]="अभी के विभाजन:"
  ["These partitions will be FORMATTED"]="ये विभाजन फ़ॉर्मैट किए जाएँगे"
  ["Full      — Standard + Steam + Nix + more software"]="पूर्ण      — मानक + Steam + Nix + और सॉफ़्टवेयर"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="मानक       — SynapseOS का सेट, Firefox, AI मॉडल,"
  ["Minimal   — core daemons only: none of the above"]="न्यूनतम    — सिर्फ़ मूल डीमन: ऊपर का कुछ भी नहीं"
  ["Custom    — tick every package yourself, ours and"]="अपनी पसंद — हर पैकेज खुद चुनिए, हमारे भी और"
  ["Which AI model should this machine run?"]="इस मशीन पर कौन-सा AI मॉडल चले?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4.1 GB   अनुशंसित — SynapseOS इसी के अनुसार ढाला गया है"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2.2 GB   आधे आकार का, और साफ़ तौर पर कमज़ोर"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0.4 GB   कहीं भी समा जाता है, जवाब भी वैसे ही"
  ["None                            skip it — nothing else changes"]="कोई नहीं                        छोड़ दीजिए — और कुछ नहीं बदलेगा"
  ["Installing:"]="स्थापित हो रहा है:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — AI के लिए बना Wayland कंपोज़िटर  (मूल विकल्प)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — उपलब्ध नहीं: synui चुना नहीं गया"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — पूरी सुविधाओं वाला Wayland डेस्कटॉप"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — साफ़-सुथरा, आधुनिक Wayland डेस्कटॉप"
  ["TTY only   — No GUI (headless/server)"]="सिर्फ़ TTY — कोई चित्रात्मक परदा नहीं (बिना परदे/सर्वर)"
  ["Disk:"]="डिस्क:"
  ["Boot:"]="बूट:"
  ["Encrypted:"]="गुप्त:"
  ["Desktop:"]="डेस्कटॉप:"
  ["User:"]="उपयोक्ता:"
  ["Hostname:"]="मेज़बान नाम:"
  ["Back up the header to another machine."]="हेडर का बैकअप किसी दूसरी मशीन पर रखिए।"
  ["%s is mounted — unmount it first\\n"]="%s माउंट है — पहले उसे अनमाउंट कीजिए
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s %s MiB का है — %s को कम से कम %s MiB चाहिए
"
  ["  Generating %s (a few seconds)...\\n"]="  %s बनाया जा रहा है (कुछ सेकंड)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="भाषा: %s  (%s, कीबोर्ड %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  इस डिस्क पर पहले से %s विभाजन, एक EFI तंत्र विभाजन (%s),
  और %s GiB खाली जगह है।
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s स्थापित करें — खाली जगह इस्तेमाल करें, बाकी सब रहने दें
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) पूरी डिस्क %s — हर विभाजन और सारा डेटा मिटा दें
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — यह डिस्क खुद विभाजित कीजिए, फिर विभाजन चुनिए
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) पूरी डिस्क %s — हर विभाजन और सारा डेटा मिटा दें  (मूल विकल्प)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — यह डिस्क खुद विभाजित कीजिए, फिर विभाजन चुनिए
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s पासफ़्रेज़ भूल गए तो डेटा गया —
  न रीसेट, न सहायता को फ़ोन, कुछ नहीं।
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper हर pacman कार्रवाई से पहले और बाद में एक स्नैपशॉट बनाता है,
  और %s में उनमें से किसी को भी बूट करने का मेन्यू बन जाता है। खराब
  उन्नयन तब बचाव-USB नहीं, बस एक रीबूट रह जाता है।
"
  ["    Disk          : %s\\n"]="    डिस्क         : %s
"
  ["    Firmware      : %s\\n"]="    फ़र्मवेयर      : %s
"
  ["    Filesystem    : %s\\n"]="    फ़ाइल तंत्र    : %s
"
  ["    Bootloader    : %s\\n"]="    बूटलोडर       : %s
"
  ["    Separate /boot: %s\\n"]="    अलग /boot: %s
"
  ["    Encryption    : %s\\n"]="    गोपन          : %s
"
  ["    Snapshots     : %s\\n"]="    स्नैपशॉट      : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  %s गुप्त किया जा रहा है (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  मूल विभाजन फ़ॉर्मैट किया जा रहा है (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • रखें    सभी %s मौजूदा विभाजन, Windows सहित
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • दोबारा इस्तेमाल %s को EFI विभाजन के रूप में (माउंट, %s फ़ॉर्मैट)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • बनाएँ   खाली जगह में लगभग %s GiB का नया ext4 मूल
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  खाली जगह में मूल विभाजन बनाया जा रहा है (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  नया मूल फ़ॉर्मैट किया जा रहा है (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s बाहर निकलते ही इंस्टॉलर तालिका दोबारा पढ़ेगा।
"
  ["    • a root partition, at least %s GiB\\n"]="    • एक मूल विभाजन, कम से कम %s GiB का
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • लगभग 1 GiB का अलग /boot — इस बनावट में %s मूल को पढ़ नहीं सकता
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  %s पर %s शुरू किया जा रहा है — निकलने से पहले अपने बदलाव लिख दीजिए।
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s पहले से स्वैप है — कोई दूसरा तंत्र उससे जाग सकता है।
"
  ["  Everything else on %s is left untouched.\\n"]="  %s पर बाकी सब वैसा ही रहेगा।
"
  ["  Making swap on %s...\\n"]="  %s पर स्वैप बनाया जा रहा है...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  EFI विभाजन फ़ॉर्मैट किया जा रहा है (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  NVIDIA GPU मिला — %s स्थापित किया जा रहा है (मॉड्यूल बनाता है, समय लगेगा)...
"
  ["  Installing video stack: %s %s...\\n"]="  वीडियो का ढाँचा स्थापित किया जा रहा है: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] चालू (%s पैकेज उपलब्ध)
"
  ["  Language: %s  (chosen at boot)\\n"]="  भाषा: %s  (बूट के समय चुनी गई)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  लोकेल:  %s   कीबोर्ड: %s (कंसोल) / %s (डेस्कटॉप)
"
  ["  Installing fonts (%s)...\\n"]="  फ़ॉन्ट स्थापित किए जा रहे हैं (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  AI मॉडल उतारा जा रहा है (%s) — यही सबसे लंबा हिस्सा है
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  अभी कुछ भी नहीं बना है। %s के रूप में, पहली बूट के बाद:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  mkinitcpio में %s हुक जोड़ा जा रहा है...
"
  ["  Installing GRUB (%s)...\\n"]="  GRUB स्थापित किया जा रहा है (%s)...
"
  ["yes — LUKS2 on %s"]="हाँ — %s पर LUKS2"
  ["  Admin: use %s with your user password.\\n"]="  प्रशासन: अपने उपयोक्ता कूटशब्द के साथ %s इस्तेमाल कीजिए।
"
  ["  Manage it later with %s:\\n"]="  बाद में इसे %s से सँभालिए:
"
  ["  %s A damaged LUKS header\\n"]="  %s बिगड़ा हुआ LUKS हेडर
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s लाइव/बूट उपकरण पर है — वह इंस्टॉलर का अपना माध्यम है
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s पहले से FAT है — उस पर किसी दूसरे तंत्र का बूटलोडर हो सकता है।
"
  ["  Creating user '%s'...\\n"]="  उपयोक्ता '%s' बनाया जा रहा है...
"
  ["  User '%s' created (uid=%s)\\n"]="  उपयोक्ता '%s' बन गया (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  रीबूट के बाद '%s' के रूप में लॉगिन कीजिए।
"
  ["  Type '%s' to get started.\\n"]="  शुरू करने के लिए '%s' लिखिए।
"
  ["Install SynapseOS"]="SynapseOS स्थापित करें"
  ["SynapseOS packages"]="SynapseOS पैकेज"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="सिस्टम जिन चीज़ों से बना है, वे सब। जिन्हें हटाया नहीं जा सकता, वे उन चीज़ों के लिए ज़रूरी हैं जिन्हें आपने रखा है — कुछ भी स्थापित होने से पहले उन्हें दोबारा चालू करके नाम से बताया जाता है।"
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — Wayland डेस्कटॉप"
  ["synapd — the local AI daemon"]="synapd — स्थानीय AI डीमन"
  ["synsh — the AI-native shell"]="synsh — AI-मूल शेल"
  ["synguard + kernel module"]="synguard + कर्नेल मॉड्यूल"
  ["synnet — network policy"]="synnet — नेटवर्क नीति"
  ["Software — the package manager"]="Software — पैकेज प्रबंधक"
  ["Files — the file manager"]="फ़ाइलें — फ़ाइल प्रबंधक"
  ["Terminal (synui depends on it)"]="टर्मिनल (synui को इसकी ज़रूरत है)"
  ["Settings"]="सेटिंग्स"
  ["Disks"]="डिस्क"
  ["Editor"]="संपादक"
  ["Calendar"]="कैलेंडर"
  ["File Vault — a locked folder"]="फ़ाइल तिजोरी — एक बंद फ़ोल्डर"
  ["Disk Cleanup — caches, and secure delete"]="डिस्क सफ़ाई — कैश और सुरक्षित मिटाना"
  ["syn-update — how fixes arrive"]="syn-update — सुधार कैसे पहुँचते हैं"
  ["syn — the top-level CLI"]="syn — सबसे ऊपरी कमांड लाइन"
  ["syn-model — fetch AI models"]="syn-model — AI मॉडल लाएँ"
  ["syn-confine — the sandbox"]="syn-confine — सैंडबॉक्स"
  ["fetch — the About OS readout"]="fetch — सिस्टम का विवरण"
  ["Arcade — overlay, pads, big screen"]="Arcade — ओवरले, गेमपैड, बड़ी स्क्रीन"
  ["cliamp — the music player"]="cliamp — संगीत प्लेयर"
  ["Player — playlists, shuffle and history, on mpv"]="Player — प्लेलिस्ट, फेरबदल और इतिहास, mpv पर"
  ["Studio — photo darkroom and video"]="Studio — फ़ोटो डार्करूम और वीडियो"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — ब्राउज़र में क्लाउड गेमिंग"
  ["Arsenal — BlackArch browser"]="Arsenal — BlackArch ब्राउज़र"
  ["Chibi — voice companion"]="Chibi — आवाज़ का साथी"
  ["Vibe — AI coding assistant"]="Vibe — AI कोडिंग सहायक"
  ["Animated wallpapers (~317 MB)"]="चलती वॉलपेपर (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (Firefox भी साथ आता है)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (Firefox भी साथ आता है)"
  ["Web and communication"]="वेब और संचार"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="इनमें से कुछ भी हमारा नहीं है; हर नाम Arch की रिपॉज़िटरी में है। Firefox डिफ़ॉल्ट रूप से चालू है क्योंकि पहले स्थापित SynapseOS में कोई ब्राउज़र ही नहीं आता था।"
  ["Thunderbird — mail"]="Thunderbird — मेल"
  ["KeePassXC — passwords"]="KeePassXC — पासवर्ड"
  ["Syncthing — file sync"]="Syncthing — फ़ाइल सिंक"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — फ़ोन पर भेजें (Flatpak)"
  ["Audio and video"]="ऑडियो और वीडियो"
  ["Office and graphics"]="कार्यालय और ग्राफ़िक्स"
  ["Development and admin"]="विकास और प्रशासन"
  ["VS Code (OSS build)"]="VS Code (OSS बिल्ड)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="खेल, लॉन्चर और सहायक"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam यहाँ नहीं, नीचे विकल्पों में है: यही अकेला है जो दूसरी आर्किटेक्चर और तीसरी रिपॉज़िटरी चालू करता है।"
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — गेमपैड की दोबारा सेटिंग"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="कोई कनेक्शन नहीं। SynapseOS स्थापना के दौरान ही बेस सिस्टम डाउनलोड करता है, इसलिए शुरू होने से पहले एक चलता हुआ नेटवर्क चाहिए।"
  ["Choose a disk to install to."]="स्थापना के लिए एक डिस्क चुनें।"
  ["The encryption passphrase needs at least 8 characters."]="एन्क्रिप्शन पासफ़्रेज़ में कम से कम 8 अक्षर चाहिए।"
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="न पैकेज प्रबंधक और न डेस्कटॉप — तब इस स्थापना के पास दोनों में से किसी को वापस जोड़ने का कोई रास्ता नहीं रहेगा। कम से कम एक रखें।"
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="उपयोगकर्ता नाम में छोटे अक्षर, अंक, - और _ होते हैं, और यह अंक से शुरू नहीं हो सकता।"
  ["Set a password for the account."]="खाते के लिए एक पासवर्ड तय करें।"
  ["The two passwords do not match."]="दोनों पासवर्ड मेल नहीं खाते।"
  ["A locale is needed, e.g. en_US.UTF-8."]="एक लोकेल चाहिए, जैसे hi_IN.UTF-8।"
  ["A timezone is needed, e.g. Europe/Lisbon."]="एक समय क्षेत्र चाहिए, जैसे Asia/Kolkata।"
  ["printing"]="छपाई"
  ["%1 repo"]="%1 रिपॉज़िटरी"
  ["Disk"]="डिस्क"
  ["Mode"]="ढंग"
  ["Filesystem"]="फ़ाइल सिस्टम"
  ["%1 on LUKS2"]="LUKS2 पर %1"
  ["%1 + snapshots"]="%1 + स्नैपशॉट"
  ["Install"]="स्थापना"
  ["none"]="कोई नहीं"
  ["Account"]="खाता"
  ["Desktop"]="डेस्कटॉप"
  ["Locale"]="लोकेल"
  ["%1   keys %2 / %3"]="%1   कुंजियाँ %2 / %3"
  ["Timezone"]="समय क्षेत्र"
  ["%1 package(s) — WITHOUT %2"]="%1 पैकेज — %2 के बिना"
  ["%1 package(s)"]="%1 पैकेज"
  ["Software"]="Software"
  ["Options"]="विकल्प"
  ["Could not write the install profile."]="स्थापना प्रोफ़ाइल नहीं लिखी जा सकी।"
  ["Installation complete."]="स्थापना पूरी हुई।"
  ["Installation failed — see the log."]="स्थापना विफल — लॉग देखें।"
  ["No network connection"]="कोई नेटवर्क कनेक्शन नहीं"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="बेस सिस्टम स्थापना के दौरान डाउनलोड होता है, इसलिए यह ऑफ़लाइन शुरू नहीं हो सकता। केबल लगाएँ या किसी नेटवर्क से जुड़ें, फिर “फिर से जाँचें” दबाएँ — इन पन्नों के जवाब बने रहेंगे।"
  ["Wi-Fi settings"]="Wi-Fi सेटिंग्स"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="यहाँ एक डिस्क, एक खाता और कुछ पसंद पूछी जाती हैं, और फिर वही जवाब उसी इंस्टॉलर को दिए जाते हैं जिसे टेक्स्ट संस्करण चलाता है। आख़िरी पन्ने तक किसी डिस्क पर कुछ नहीं लिखा जाता, और वह पन्ना ठीक-ठीक बताता है कि अब क्या होने वाला है।"
  ["A disk is partitioned and formatted"]="एक डिस्क विभाजित और फ़ॉर्मैट की जाती है"
  ["The base system and the SynapseOS packages are installed"]="बेस सिस्टम और SynapseOS पैकेज स्थापित किए जाते हैं"
  ["An account and a desktop are set up"]="एक खाता और एक डेस्कटॉप तैयार किया जाता है"
  ["A bootloader is written"]="एक बूटलोडर लिखा जाता है"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="मौजूदा ढाँचे को हाथ से विभाजित करना टेक्स्ट इंस्टॉलर का ADVANCED ढंग है — उसके लिए यह विंडो बंद करें और टर्मिनल में \`syn-install\` चलाएँ।"
  ["Where should SynapseOS go?"]="SynapseOS कहाँ जाए?"
  ["The installer's own media is listed and cannot be chosen."]="इंस्टॉलर का अपना मीडिया सूची में दिखता है पर चुना नहीं जा सकता।"
  ["No disks found."]="कोई डिस्क नहीं मिली।"
  ["Erase the disk"]="डिस्क मिटाएँ"
  ["every partition and all data"]="हर विभाजन और सारा डेटा"
  ["Install alongside"]="साथ में स्थापित करें"
  ["use free space, UEFI only"]="खाली जगह का उपयोग, केवल UEFI"
  ["Snapshots"]="स्नैपशॉट"
  ["btrfs + limine only"]="केवल btrfs + limine"
  ["Encrypt the disk"]="डिस्क एन्क्रिप्ट करें"
  ["Passphrase"]="पासफ़्रेज़"
  ["8 characters or more"]="8 अक्षर या अधिक"
  ["What should be installed?"]="क्या स्थापित किया जाए?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="SynapseOS का मूल — कंपोज़िटर, डीमन और वे अनुप्रयोग जिन पर यह बना है — यहाँ हर चुनाव के साथ स्थापित होता है।"
  ["Full"]="पूरा"
  ["Standard + Steam + Nix + more software"]="मानक + Steam + Nix + और सॉफ़्टवेयर"
  ["Standard"]="मानक"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="SynapseOS सुइट, Firefox, AI मॉडल, ब्लूटूथ, छपाई, Wine, फ़ोन"
  ["Minimal"]="न्यूनतम"
  ["core daemons only — no apps, no software, no model"]="केवल मूल डीमन — कोई ऐप नहीं, कोई सॉफ़्टवेयर नहीं, कोई मॉडल नहीं"
  ["Custom"]="अपनी पसंद"
  ["tick every package yourself, ours and the ordinary software"]="हर पैकेज खुद चुनें, हमारे भी और आम सॉफ़्टवेयर भी"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="ये पैकेज नहीं हैं: एक रिपॉज़िटरी, एक आर्किटेक्चर या एक सेवा। हर एक ऐसा निर्णय है जिसका नतीजा ऊपर के किसी चेकबॉक्स में नहीं समाता।"
  ["Printing (CUPS)"]="छपाई (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — Windows की .exe/.msi चलाएँ"
  ["KDE Connect — pair a phone"]="KDE Connect — फ़ोन जोड़ें"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + गेम सामग्री + Proton (~3.1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="BlackArch रिपॉज़िटरी — लगभग 5000 औज़ार, एक भी स्थापित नहीं"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update बंद है: इस मशीन के पास आगे कोई SynapseOS पैकेज पाने का रास्ता नहीं रहेगा। बाद में इसे ठीक करने का मतलब है उसे ISO से हाथ से स्थापित करना, या दोबारा स्थापित करना।"
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui बंद है: यह SynapseOS डेस्कटॉप नहीं होगा। डेस्कटॉप पन्ना KDE, GNOME या बिना GUI का विकल्प देता है।"
  ["AI model — downloaded during the install"]="AI मॉडल — स्थापना के दौरान डाउनलोड होता है"
  ["~4.1 GB — recommended"]="~4.1 GB — सुझाया गया"
  ["~2.2 GB — weaker"]="~2.2 GB — कमज़ोर"
  ["~0.4 GB — much weaker"]="~0.4 GB — बहुत कमज़ोर"
  ["None"]="कोई नहीं"
  ["AI stays inert"]="AI निष्क्रिय रहता है"
  ["NVIDIA GPU inference"]="NVIDIA GPU पर अनुमान"
  ["the CUDA runtime, ~4.7 GiB"]="CUDA रनटाइम, ~4.7 GiB"
  ["Who is this machine for?"]="यह मशीन किसके लिए है?"
  ["Username"]="उपयोगकर्ता नाम"
  ["lower-case, no spaces"]="छोटे अक्षर, बिना जगह के"
  ["Full name (optional)"]="पूरा नाम (वैकल्पिक)"
  ["Password"]="पासवर्ड"
  ["Password again"]="पासवर्ड फिर से"
  ["They do not match"]="ये मेल नहीं खाते"
  ["the native compositor"]="अपना कंपोज़िटर"
  ["synui is not selected"]="synui चुना नहीं गया है"
  ["headless"]="बिना ग्राफ़िक्स के"
  ["Language, keyboard and time"]="भाषा, कीबोर्ड और समय"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="एक भाषा चुनें और बाकी तीन उसके पीछे चलती हैं। कंसोल कीमैप और डेस्कटॉप लेआउट जान-बूझकर अलग हैं — स्वीडिश कंसोल के लिए 'sv-latin1' है और डेस्कटॉप के लिए 'se' — ताकि बाद में इन्हें अलग-अलग बदला जा सके।"
  ["Language"]="भाषा"
  ["sets the keyboard and the fonts too"]="कीबोर्ड और फ़ॉन्ट भी तय करती है"
  ["typed by hand — fonts cover as much as possible"]="हाथ से लिखा — फ़ॉन्ट जितना हो सके उतना ढकते हैं"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="लोकेल, दोनों कीबोर्ड नाम और फ़ॉन्ट पैक तय करता है। इसकी जगह glibc जो भी लोकेल जानता है, वह लिखा जा सकता है।"
  ["The common zones first, then every name tzdata ships."]="पहले आम क्षेत्र, फिर हर वह नाम जो tzdata देता है।"
  ["Console keymap"]="कंसोल कीमैप"
  ["loadkeys — the text console and the greeter"]="loadkeys — टेक्स्ट कंसोल और लॉगिन स्क्रीन"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="हर कीमैप जिसे यह इमेज लोड कर सकती है। यह उस फ़ाइल का नाम है जिसे loadkeys को ढूँढ़ना है, इसीलिए यह डेस्कटॉप लेआउट वाली सूची नहीं है।"
  ["Desktop layout"]="डेस्कटॉप लेआउट"
  ["XKB — the compositor"]="XKB — कंपोज़िटर"
  ["Desktop keyboard layout"]="डेस्कटॉप कीबोर्ड लेआउट"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="वे लेआउट जिन्हें xkbcommon संकलित कर सकता है। 'uk' एक कंसोल कीमैप है और यहाँ लेआउट नहीं है — यहाँ लेआउट 'gb' है।"
  ["Read this back"]="इसे दोबारा पढ़ें"
  ["Nothing has been written yet. The next button is the one that starts."]="अभी तक कुछ नहीं लिखा गया है। अगला बटन ही वह है जो शुरू करता है।"
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="%1 का हर विभाजन मिटा दिया जाएगा"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS %1 की खाली जगह में स्थापित किया जाएगा"
  ["SynapseOS is installed"]="SynapseOS स्थापित हो गया"
  ["The install stopped"]="स्थापना रुक गई"
  ["Installing SynapseOS"]="SynapseOS स्थापित हो रहा है"
  ["Reboot and remove the installation media."]="दोबारा चालू करें और स्थापना मीडिया निकाल लें।"
  ["The log below is the whole story — the last lines say why."]="नीचे का लॉग पूरी कहानी है — आख़िरी पंक्तियाँ कारण बताती हैं।"
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="इसमें समय लगता है: बेस सिस्टम और पैकेज डाउनलोड होते हैं, और AI मॉडल अकेले ही कई गीगाबाइट का है।"
  ["Back"]="पीछे"
  ["Next"]="आगे"
  ["Reboot"]="दोबारा चालू करें"
  ["Close"]="बंद करें"
  ["type to filter, or type a name that is not listed"]="छाँटने के लिए लिखें, या ऐसा नाम लिखें जो सूची में नहीं है"
  ["Nothing to list on this image — type the name instead."]="इस इमेज पर दिखाने को कुछ नहीं है — इसके बदले नाम लिखें।"
  ["Nothing matches — the row below uses what you typed."]="कुछ मेल नहीं खाता — नीचे की पंक्ति वही लेती है जो आपने लिखा।"
  ["Use “%1” as typed"]="“%1” जैसा लिखा है वैसा ही लें"
)
