# العربية (ar) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix غير موجود — حزمة 'syn' ليست مثبَّتة هنا."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="فشل locale-gen. تبقى الجلسة الحيّة بالإنجليزية؛ أما التثبيت فلا يتأثر،
  لأنه يولّد اللغة داخل النظام الهدف."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  لوحة المفاتيح والساعة والخطوط والصدفة، كلها تتبع هذا الاختيار.
  ويمكن تغيير أيٍّ منها لاحقًا."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="تبديل [أرقام، 'all'، 'none'، Enter = قبول]:"
  ["--config needs a file"]="‏--config يحتاج إلى ملف"
  ["syn-install must be run as root"]="يجب تشغيل syn-install بصلاحية root"
  ["  SynapseOS is running from the live image."]="  يعمل SynapseOS من الصورة الحيّة."
  ["Starting the desktop — the installer opens with it."]="يجري بدء سطح المكتب — وسيُفتح المثبِّت معه."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  سيقوم هذا المثبِّت بما يلي:
    1. تقسيم قرص
    2. تثبيت نظام SynapseOS الأساسي
    3. تثبيت حزم SynapseOS
    4. إنشاء حساب مستخدم
    5. اختيار بيئة سطح مكتب
    6. ضبط النظام ومحمِّل الإقلاع"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="ستُمحى كل البيانات على القرص الهدف"
  ["Press ENTER to continue or Ctrl+C to abort..."]="اضغط ENTER للمتابعة، أو Ctrl+C للإلغاء..."
  ["Checking network"]="يجري فحص الشبكة"
  ["Network is up"]="الشبكة تعمل"
  ["  No network detected. Starting NetworkManager..."]="  لم يُعثر على شبكة. يجري تشغيل NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  لا يوجد اتصال — لكن في هذا الجهاز واي-فاي."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="أفتح مُحدِّد الواي-فاي (nmtui)؟ [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="لا اتصال بالشبكة ولا جهاز واي-فاي لضبطه.
  ينزّل SynapseOS النظام الأساسي أثناء التثبيت، فصِل كابلًا ثم أعد
  التشغيل."
  ["Network connected"]="تم الاتصال بالشبكة"
  ["Step 1 — Select Target Disk"]="الخطوة 1 — اختيار القرص الهدف"
  ["  Available disks:"]="  الأقراص المتاحة:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="القرص الهدف (مثل sda أو vda أو nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="القرص الهدف قيد الاستعمال. افصل أقسامه ثم أعد التشغيل."
  ["Boot mode: UEFI"]="وضع الإقلاع: UEFI"
  ["Boot mode: BIOS/Legacy"]="وضع الإقلاع: BIOS/التقليدي"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  يعمّي نظام الملفات الجذر باستخدام LUKS2. ستُطلب عبارة المرور عند كل
  إقلاع، قبل أن يبدأ النظام."
  ["Encrypt the disk? [y/N]:"]="أعمّي القرص؟ [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          مع التعمية هو الخيار الأفضل: النواة على قسم
                          EFI ولا يفكّ القفل سوى initramfs، فلا يحتاج
                          ‏/boot إلى قسم منفصل غير معمَّى."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          ينسخ نواة كل لقطة إلى قسم EFI، ولذلك يُنشأ
                          ذلك القسم أكبر بكثير عند تفعيل اللقطات."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  اللقطات رخيصة لكنها ليست مجانية: تحتفظ بالنسخة القديمة من كل ما
  يتغيّر، فالقرص الممتلئ تقريبًا يبقى ممتلئًا تقريبًا."
  ["Enable snapshots? [Y/n]:"]="أفعّل اللقطات؟ [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="‏mkfs.ext4 غير موجود في صورة المثبِّت هذه — لا يمكن إنشاء ‏/boot"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="‏btrfs غير موجود في صورة المثبِّت هذه — لا يمكن إنشاء الأقسام الفرعية"
  ["Are these correct? [Y/n]:"]="أهذا صحيح؟ [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="نبدأ الأسئلة من جديد — لم يُمَس القرص."
  ["cryptsetup is not available on this installer image"]="‏cryptsetup غير متاح في صورة المثبِّت هذه"
  ["Encryption passphrase:"]="عبارة مرور التعمية:"
  ["Repeat passphrase:"]="أعد عبارة المرور:"
  ["Empty passphrase — that would leave the disk unprotected."]="عبارة المرور فارغة — هذا يترك القرص بلا حماية."
  ["Passphrases did not match — try again."]="عبارتا المرور غير متطابقتين — أعد المحاولة."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="عبارة المرور أقصر من 8 محارف. القصيرة لا تفيد كثيرًا أمام من
  يمسك القرص بيده."
  ["Use it anyway? [y/N]:"]="أستعملها رغم ذلك؟ [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="فُعِّلت التعمية — سيكون الجذر على LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="فشل cryptsetup open — لم تُقبَل عبارة المرور"
  ["Failed to mount root"]="تعذّر وصل الجذر"
  ["  Creating btrfs subvolumes..."]="  يجري إنشاء أقسام btrfs الفرعية..."
  ["btrfs: could not create @"]="‏btrfs: تعذّر إنشاء @"
  ["btrfs: could not create @home"]="‏btrfs: تعذّر إنشاء @home"
  ["btrfs: could not create @snapshots"]="‏btrfs: تعذّر إنشاء @snapshots"
  ["btrfs: could not create @var_log"]="‏btrfs: تعذّر إنشاء @var_log"
  ["btrfs: could not create @pkg"]="‏btrfs: تعذّر إنشاء @pkg"
  ["could not remount the btrfs root onto @"]="تعذّرت إعادة وصل جذر btrfs على @"
  ["Failed to mount @"]="تعذّر وصل @"
  ["Failed to mount @home"]="تعذّر وصل @home"
  ["Failed to mount @snapshots"]="تعذّر وصل @snapshots"
  ["Failed to mount @var_log"]="تعذّر وصل @var_log"
  ["Failed to mount @pkg"]="تعذّر وصل @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="هذا يضيف قسمًا واحدًا في المساحة الحرة. احتفظ أولًا بنسخة من كل ما لا يمكن تعويضه."
  ["Type 'yes' to install alongside:"]="اكتب 'yes' للتثبيت جنبًا إلى جنب:"
  ["Aborted"]="أُلغي"
  ["Failed to create the root partition"]="تعذّر إنشاء قسم الجذر"
  ["Could not identify the new partition after creating it"]="تعذّر التعرّف على القسم الجديد بعد إنشائه"
  ["Failed to format root partition"]="تعذّر تهيئة قسم الجذر"
  ["Failed to mount the existing ESP"]="تعذّر وصل قسم ESP الموجود"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="لا يوجد محرِّر أقسام في هذه الصورة (لا cfdisk ولا fdisk ولا parted)"
  ["  What this install needs:"]="  ما يحتاجه هذا التثبيت:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • قسم نظام EFI (النوع EF00 / راية 'esp') — ويمكن إعادة استعمال قسم موجود"
  ["  Skipping the partition editor (--config)."]="  يجري تخطّي محرِّر الأقسام (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="أهيّئه؟ كل ما عليه سيضيع [y/N]:"
  ["Separate /boot partition:"]="قسم ‏/boot منفصل:"
  ["Swap partition (blank for none):"]="قسم التبديل (اتركه فارغًا لعدم استعماله):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="أعيد إنشاءه؟ سيتغيّر معرّفه UUID فينكسر ملف fstab لذلك النظام [y/N]:"
  ["Type 'yes' to format these:"]="اكتب 'yes' لتهيئة هذه الأقسام:"
  ["  Formatting EFI partition..."]="  يجري تهيئة قسم EFI..."
  ["  Formatting /boot partition..."]="  يجري تهيئة قسم ‏/boot..."
  ["Failed to mount /boot"]="تعذّر وصل ‏/boot"
  ["Type 'yes' to confirm:"]="اكتب 'yes' للتأكيد:"
  ["  Creating GPT partition table..."]="  يجري إنشاء جدول أقسام GPT..."
  ["Failed to format EFI partition"]="تعذّرت تهيئة قسم EFI"
  ["Failed to format boot partition"]="تعذّرت تهيئة قسم الإقلاع"
  ["  Creating MBR partition table..."]="  يجري إنشاء جدول أقسام MBR..."
  ["Disk partitioned and mounted at /mnt"]="قُسِّم القرص ووُصِل على ‏/mnt"
  ["Step 3 — Installing Base System"]="الخطوة 3 — تثبيت النظام الأساسي"
  ["  Initializing pacman keyring..."]="  يجري تهيئة حلقة مفاتيح pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  يجري تنفيذ pacstrap (قد يستغرق عدة دقائق)..."
  ["pacstrap failed — check network connection"]="فشل pacstrap — تحقق من الاتصال بالشبكة"
  ["grub-install not found in chroot — attempting recovery..."]="‏grub-install غير موجود داخل chroot — تجري محاولة للإصلاح..."
  ["Could not install grub into target — check network"]="تعذّر تثبيت grub في النظام الهدف — تحقق من الشبكة"
  ["Base system installed"]="ثُبِّت النظام الأساسي"
  ["Step 4 — Choose What to Install"]="الخطوة 4 — اختيار ما سيُثبَّت"
  ["  What should be installed alongside the SynapseOS core?"]="  ماذا نثبّت إلى جانب نواة SynapseOS؟"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   بلوتوث، طباعة، Wine، الهاتف   (الافتراضي)"
  ["                   the ordinary software people install anyway"]="                   البرامج المعتادة التي يثبّتها الناس على أي حال"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  كل نمط عدا الأدنى يسأل بعد ذلك أي نموذج ذكاء اصطناعي يُنزَّل،
  وتخطّيه أحد الأجوبة."
  ["Full install selected"]="اختير التثبيت الكامل"
  ["Minimal install selected"]="اختير التثبيت الأدنى"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  الأسئلة نوعان. أولًا الحزم، في صفحات من مربّعات الاختيار؛
  ثم تلك الخيارات القليلة التي هي نظام فرعي كامل لا حزمة."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  ثم البرامج التي يثبّتها الناس في أول أمسية على أي حال.
  كلها في مستودعات Arch؛ ولا شيء منها لنا."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  والباقي بنعم/لا (y/n). والافتراضي (بالأحرف الكبيرة) هو القياسي."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="أُلغي اختيار syn-update: لن يبقى لهذا الجهاز أي وسيلة لتلقّي حزمة
  SynapseOS تالية. إصلاح ذلك لاحقًا يعني تثبيتها يدويًا من ملف ISO،
  أو إعادة التثبيت."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="لم يُبقَ لا على سطح المكتب ولا على خديم الذكاء الاصطناعي. ذاك نظام
  Arch عليه بعض أدوات SynapseOS، وهو جواب مقبول —
  لكن لن تجد في أي وثيقة وصفًا للجهاز الذي ستحصل عليه."
  ["Custom install configured"]="ضُبِط التثبيت المخصّص"
  ["Standard install selected"]="اختير التثبيت القياسي"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  يحمّل synapd نموذجًا واحدًا، وكل ما يخص الذكاء الاصطناعي في SynapseOS
  يتحدث إليه: synsh، ولوحة الذكاء الاصطناعي في سطح المكتب، وChibi، وVibe.
  يُنزَّل الآن، عبر هذا الاتصال، إلى القرص الذي تثبّت عليه."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  النموذج الأصغر ليس أسرع وأخف فحسب: إنه يتبع التعليمات على نحو أسوأ.
  يسيء synsh فهم ما طلبته، ويحتاج كود Vibe إلى تصحيح أكثر، ويفقد Chibi
  خيط الحديث. خذ الافتراضي ما لم يمنع القرص أو الذاكرة —
  فـ 7B يريد نحو 6 غيغابايت من الذاكرة الحرة."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  أيًّا كان اختيارك، يمكن تغييره لاحقًا: 'syn model download'،
  أو Super+C ▸ النظام ▸ نموذج الذكاء الاصطناعي على سطح المكتب."
  ["Install this selection? [Y/n]:"]="أثبّت هذا الاختيار؟ [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="نختار من جديد — لم يُثبَّت شيء بعد."
  ["Step 4b — Installing SynapseOS"]="الخطوة 4b — تثبيت SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="تعذّر تفعيل ILoveCandy في ‏/etc/pacman.conf (شكلي فقط)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  يجري تفعيل [multilib] (مستودع 32 بت، يحتاجه Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="تعذّرت مزامنة قاعدة multilib — قد يفشل تثبيت Steam"
  ["Could not enable [multilib]; Steam will be skipped."]="تعذّر تفعيل [multilib]؛ سيُتخطّى Steam."
  ["Some SynapseOS packages failed to install — verifying below"]="لم تُثبَّت بعض حزم SynapseOS — سيجري التحقق أدناه"
  ["No SynapseOS packages were selected. This will be an Arch system."]="لم تُختَر أي حزمة من SynapseOS. سيكون هذا نظام Arch."
  ["SynapseOS packages installed"]="ثُبِّتت حزم SynapseOS"
  ["Component selection recorded in /etc/synapseos/components.conf"]="سُجِّل اختيار المكوّنات في ‏/etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="الخطوة 5 — إنشاء حساب مستخدم"
  ["  Create a user account for the installed system."]="  أنشئ حساب مستخدم للنظام المثبَّت."
  ["Username [default: syn]:"]="اسم المستخدم [الافتراضي: syn]:"
  ["Full name (optional):"]="الاسم الكامل (اختياري):"
  ["Password:"]="كلمة السر:"
  ["Confirm password:"]="أكّد كلمة السر:"
  ["Passwords do not match or are empty — try again"]="كلمتا السر غير متطابقتين أو فارغتان — أعد المحاولة"
  ["Step 6 — Desktop Environment"]="الخطوة 6 — بيئة سطح المكتب"
  ["  Choose a desktop environment:"]="  اختر بيئة سطح مكتب:"
  ["  Installing KDE Plasma..."]="  يجري تثبيت KDE Plasma..."
  ["Some KDE packages failed to install"]="لم تُثبَّت بعض حزم KDE"
  ["KDE Plasma installed"]="ثُبِّت KDE Plasma"
  ["  Installing GNOME..."]="  يجري تثبيت GNOME..."
  ["Some GNOME packages failed to install"]="لم تُثبَّت بعض حزم GNOME"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="ثُبِّت GNOME (الجلسة فقط — التطبيقات من SynapseOS لا من GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  يجري تثبيت greetd (شاشة الولوج) وإضافات سطح المكتب..."
  ["greetd failed to install — boot falls back to getty login"]="لم يُثبَّت greetd — سيعود الإقلاع إلى الولوج عبر getty"
  ["SynapseUI selected (included)"]="اختير SynapseUI (مضمَّن)"
  ["Installing Wine"]="يجري تثبيت Wine"
  ["Wine installed"]="ثُبِّت Wine"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="لم يُثبَّت wine — لن تعمل ملفات ‏.exe/.msi الخاصة بويندوز.
  ثبّته لاحقًا بـ 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="ضبط تعريف العرض"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  آلة افتراضية — يجري تثبيت mesa (يستعمل synui هنا pixman)..."
  ["mesa failed to install"]="لم تُثبَّت mesa"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="فشل تثبيت تعريف NVIDIA — عندها سيقلع النظام على nouveau
  ولن يبدأ مُصيِّر synui أبدًا"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="فُعِّلت خدمات سبات NVIDIA (حفظ ذاكرة العرض واستعادتها)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="تعذّر تفعيل nvidia-{suspend,resume,hibernate} — قد تبقى الشاشة
  سوداء بعد السبات إذا فُعِّل NVreg_PreserveVideoMemoryAllocations لاحقًا"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  يستطيع synapd أن يستنتج على هذا المعالج الرسومي بدل المعالج المركزي.
  وهذا ينزّل بيئة CUDA (نحو 4.7 غيغابايت بعد التثبيت)."
  ["Enable GPU inference? [Y/n]:"]="أفعّل الاستنتاج على المعالج الرسومي؟ [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="سيبقى الاستنتاج على المعالج المركزي. للتبديل لاحقًا:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  يجري تثبيت synapse-llama-cuda (سيستغرق بعض الوقت)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="لا تتضمن هذه الصورة بناءً لـ llama للمعالج الرسومي، فسيعمل synapd على
  المعالج المركزي رغم بطاقة NVIDIA. (يجب بناء الصورة على مضيف فيه عدة
  CUDA حتى يوجد synapse-llama-cuda.)"
  ["Video driver install failed — synui may fall back to software rendering"]="فشل تثبيت تعريف العرض — قد يعود synui إلى التصيير البرمجي"
  ["Video drivers installed"]="ثُبِّتت تعريفات العرض"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  يجري تفعيل الاستنتاج على المعالج الرسومي (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="لا تتضمن هذه الصورة بناءً لـ llama لـ Vulkan، فسيعمل synapd على المعالج
  المركزي رغم معالج AMD/Intel الرسومي. (ابنِ الصورة على مضيف فيه 'shaderc'
  وvulkan-headers حتى يوجد synapse-llama-vulkan.)"
  ["Installing Steam and the game stack"]="تثبيت Steam وطبقة الألعاب"
  ["  Installing steam and the 32-bit runtime libraries..."]="  يجري تثبيت steam والمكتبات التنفيذية بـ 32 بت..."
  ["Steam installed (native multilib package)"]="ثُبِّت Steam (حزمة multilib أصلية)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="لم يُثبَّت steam. النظام مكتمل فيما عدا ذلك —
  ثبّته لاحقًا بـ 'sudo pacman -S steam' ([multilib] مفعَّل مسبقًا
  في ‏/etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  يجري تثبيت طبقة الألعاب (طبقة العرض، منظّم التردد، المؤلِّف المصغّر)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="ثُبِّتت طبقة الألعاب (mangohud وgamemode وgamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="لم تُثبَّت طبقة الألعاب. ما زال Steam يعمل؛ أما طبقة عرض الإطارات
  ومنظّم تردد المعالجين و'synui-game-run --gamescope' فلا.
  ثبّتها لاحقًا بـ:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="تثبيت CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  يجري جلب حلقة مفاتيح CachyOS وقائمة المرايا..."
  ["  Trusting the CachyOS master key..."]="  يجري منح الثقة لمفتاح CachyOS الرئيس..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="تعذّر جلب مفتاح CachyOS الرئيس من keyserver.ubuntu.com.
  ومن دونه لا يمكن تثبيت حلقة المفاتيح الموقَّعة، فتُخطّى CachyOS Proton.
  أضِفها لاحقًا بـ:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  المفتاح الرئيس كما هو متوقَّع — يجري منحه الثقة..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="أُضيف [cachyos] لكنه لا يسرد أي حزمة — تجري إزالته
  مرة أخرى كي لا يعيق ترقية لاحقة."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="حلقة مفاتيح CachyOS لا تحمل المفتاح الرئيس المتوقَّع.
  رُفضت الثقة — ولم يُضَف المستودع."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  يجري تثبيت proton-cachyos-slr (تنزيل نحو 340 ميغابايت)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="ثُبِّت CachyOS Proton — اختره لكل لعبة في Steam من الخصائص →
  التوافق، وهو مدرج باسم 'proton-cachyos-… (steam linux runtime)'.
  لا يبحث عنه Steam إلا عند بدء تشغيله، فأعد تشغيله إن كان يعمل."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="لم يُثبَّت proton-cachyos-slr. ولا يتأثر Steam ولا Proton الخاص بـ Valve.
  ثبّته لاحقًا بـ:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="تعذّر تفعيل مستودع [cachyos]، فتُخطّيت CachyOS Proton.
  وما زال Steam يعمل مع Proton الخاص بـ Valve. للإضافة لاحقًا:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="تفعيل BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  يجري جلب سكربت تهيئة BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  المفتاح الرئيس كما هو متوقَّع — يجري تنفيذ السكربت..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="لم يُثبَّت blackarch-keyring — لن تصل تبديلات المفاتيح
  إلى هذا الجهاز. أصلحه بـ 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="ملف strap.sh المنزَّل لا يثبّت مفتاح BlackArch الرئيس المتوقَّع.
  رُفض تنفيذه — ولم يُضَف المستودع."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="لم يُفعَّل BlackArch. والنظام مكتمل فيما عدا ذلك؛
  أضِفه لاحقًا بـ 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="تثبيت البرامج"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="فشلت تلك العملية — تُعاد المحاولة لكل حزمة على حدة، كي تصل السليمة
  على أي حال ويُسمّى ما ليس سليمًا."
  ["Software installed"]="ثُبِّتت البرامج"
  ["Installing Flatpak apps"]="تثبيت تطبيقات Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="تعذّر تثبيت flatpak — ستُتخطّى تطبيقات Flatpak.
  ولا يتأثر شيء آخر."
  ["Could not add the flathub remote"]="تعذّرت إضافة مستودع flathub البعيد"
  ["Flatpak apps installed"]="ثُبِّتت تطبيقات Flatpak"
  ["Configuring System"]="ضبط النظام"
  ["  fstab generated"]="  وُلِّد ملف fstab"
  ["Swap recorded in fstab"]="سُجِّل التبديل في fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="ضُبِط zram (تبديل مضغوط، نصف الذاكرة بحد أقصى 8 غيغابايت)"
  ["zram-generator is not installed in the target — no compressed swap"]="‏zram-generator غير مثبَّت في النظام الهدف — لا تبديل مضغوط"
  ["  Hostname: synapse"]="  اسم المضيف: synapse"
  ["Step 7 — Language & Region"]="الخطوة 7 — اللغة والمنطقة"
  ["   0) Other — enter a locale by hand"]="   0) غير ذلك — أدخل اللغة يدويًا"
  ["Locale (e.g. sv_SE.UTF-8):"]="اللغة (مثل sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="خريطة مفاتيح الطرفية (مثل sv-latin1):"
  ["Step 8 — Timezone"]="الخطوة 8 — المنطقة الزمنية"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) غير ذلك — أدخل أي اسم من tzdata (مثل Europe/Lisbon)"
  ["tzdata name (Region/City):"]="اسم tzdata (المنطقة/المدينة):"
  ["  Did you mean:"]="  هل تقصد:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  اختر رقمًا من القائمة، أو انظر: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: منسوخ من النظام الحيّ"
  ["  issue: copied from live system"]="  issue: منسوخ من النظام الحيّ"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="لم يعد نظام الملفات الهدف قابلًا للكتابة (أخطاء قرص؟ راجع 'dmesg') — إلغاء"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="مجموعة قواعد sudoers غير صالحة بعد كتابة الملفات الإضافية — لن يُسلَّم نظام لا يستطيع sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="تعذّر تخفيف pam_faillock في ‏/etc/pam.d/system-auth (قد يظل sudo بلا طرفية قادرًا على قفل الحساب حتى إعادة التشغيل)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="تعذّر إنشاء ‏/var/lib/synapse-src مسبقًا — ستطلب أداة التحديث كلمة سر عند أول تشغيل"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  سطح المكتب: KDE Plasma (شاشة ولوج SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: شعار SynapseOS على شاشة الولوج"
  ["  Desktop: GNOME (GDM login screen)"]="  سطح المكتب: GNOME (شاشة ولوج GDM)"
  ["  Desktop: TTY only"]="  سطح المكتب: طرفية نصية فقط"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  سطح المكتب: SynapseUI (مُرحِّب synui — شاشة الولوج تطابق شاشة القفل)"
  ["  motd: written for this installation"]="  motd: كُتب لهذا التثبيت"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  ملاحظة: syn-rgb.path غير مثبَّت؛ ستبقى أضواء RGB مطفأة"
  ["AI model"]="نموذج الذكاء الاصطناعي"
  ["  AI model skipped — install one later with: syn model download"]="  تُخطّي نموذج الذكاء الاصطناعي — ثبّت واحدًا لاحقًا بـ: syn model download"
  ["AI model installed"]="ثُبِّت نموذج الذكاء الاصطناعي"
  ["  the install, and everything else on the disk is already done."]="  من التثبيت، وكل ما عدا ذلك على القرص قد انتهى."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="‏syn-model غير موجود في النظام الهدف، فلم يُنزَّل أي نموذج.
  وهو من المجموعة الأساسية؛ فإن أُلغي اختياره بقي الذكاء الاصطناعي خاملًا."
  ["Configuring Nix"]="ضبط Nix"
  ["Nix configured — /etc/synapseos/nix"]="ضُبِط Nix — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  هذا هو التنزيل — بضع مئات من الميغابايت قبل أي حزمة تضيفها
  إلى home.nix. و'syn nix edit' يفتحه."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="ثُبِّت nix، لكن حزمة 'syn' ليست في النظام الهدف، فلم تُهيَّأ أداة
  الضبط. أما Nix نفسه فيعمل؛
  وطبقة ‏/etc/synapseos/nix تحتاج إلى 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="لم يُثبَّت nix — الطبقة التصريحية غير متاحة.
  ثبّتها لاحقًا بـ 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  يجري توليد initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="فشل mkinitcpio — لن يُقلع النظام المثبَّت"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="لا يوجد initramfs بعد mkinitcpio — لن يُقلع النظام المثبَّت"
  ["System configured"]="ضُبِط النظام"
  ["Installing Bootloader"]="تثبيت محمِّل الإقلاع"
  ["grub-install (UEFI) failed"]="فشل grub-install (UEFI)"
  ["grub-install (BIOS) failed"]="فشل grub-install (BIOS)"
  ["  Generating GRUB config..."]="  يجري توليد إعدادات GRUB..."
  ["grub-mkconfig failed"]="فشل grub-mkconfig"
  ["grub.cfg missing after install"]="‏grub.cfg غير موجود بعد التثبيت"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="يحمل grub.cfg كلمة سر GRUB — تُرك مقروءًا لـ root وحده، فلا يستطيع تطبيق الإعدادات الإخبار عن مدخلات الإقلاع"
  ["  Installing systemd-boot..."]="  يجري تثبيت systemd-boot..."
  ["bootctl install failed"]="فشل bootctl install"
  ["  Registering systemd-boot with the firmware..."]="  يجري تسجيل systemd-boot لدى البرنامج الثابت..."
  ["efibootmgr entry not created — the removable-media path still applies"]="لم يُنشأ مدخل efibootmgr — ما زال مسار الوسائط القابلة للإزالة ساريًا"
  ["could not read the root filesystem UUID"]="تعذّرت قراءة معرّف UUID لنظام الملفات الجذر"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="‏vmlinuz-linux ليس على ESP — لن يجد systemd-boot ما يُقلع منه"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="‏initramfs ليس على ESP — لن يجد systemd-boot ما يُقلع منه"
  ["systemd-boot did not install its EFI binary"]="لم يثبّت systemd-boot ملفه التنفيذي EFI"
  ["  Installing limine..."]="  يجري تثبيت limine..."
  ["could not copy limine's EFI binary to the ESP"]="تعذّر نسخ ملف limine التنفيذي EFI إلى ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="‏limine-mkinitcpio-hook غير مثبَّت — لن يحصل أي نواة تُثبَّت لاحقًا على مدخل إقلاع"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="‏vmlinuz-linux ليس على ESP — لن يجد limine ما يُقلع منه"
  ["limine's EFI binary is not on the ESP"]="ملف limine التنفيذي EFI ليس على ESP"
  ["limine.conf has no kernel entry"]="لا يوجد في limine.conf أي مدخل نواة"
  ["  Verifying the encrypted boot path..."]="  يجري التحقق من مسار الإقلاع المعمَّى..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="‏/boot ليس نقطة وصل منفصلة — الجذر المعمَّى يحتاج إلى ‏/boot غير معمَّى"
  ["/boot is missing from fstab — it would not be mounted after boot"]="‏/boot غير موجود في fstab — فلن يُوصَل بعد الإقلاع"
  ["Encrypted boot path verified"]="تم التحقق من مسار الإقلاع المعمَّى"
  ["Configuring snapshots"]="ضبط اللقطات"
  ["snapper's config template is missing — snapshots cannot be configured"]="قالب إعدادات snapper غير موجود — لا يمكن ضبط اللقطات"
  ["could not write /etc/snapper/configs/root"]="تعذّرت كتابة ‏/etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="لا يرى snapper إعداد 'root' — فلن تُؤخذ أي لقطة أبدًا"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="لم يُضبَط إعداد root في snapper — ستملأ اللقطات الدورية القرص"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="تعذّر تفعيل grub-btrfsd — لن تظهر اللقطات في قائمة الإقلاع تلقائيًا"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="فُعِّلت اللقطات (snapper + snap-pac، ويمكن الإقلاع منها عبر GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="تعذّر تفعيل limine-snapper-sync — لن تصل اللقطات إلى قائمة الإقلاع تلقائيًا"
  ["could not take the post-install snapshot"]="تعذّر أخذ اللقطة التالية للتثبيت"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="تعذّر تفعيل مزامنة اللقطات عند أول إقلاع — ستمتلئ القائمة بعد أول ترقية بدلًا من ذلك"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="فُعِّلت اللقطات (snapper + snap-pac، ويمكن الإقلاع منها عبر limine)"
  ["Bootloader installed"]="ثُبِّت محمِّل الإقلاع"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  حساب root مقفل (لا ولوج بـ root ولا su).
  ملاحظة: ثلاث كلمات سر خاطئة تقفل الحساب عشر دقائق."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="ستُطلب عبارة مرور التعمية عند كل إقلاع، قبل شاشة الولوج.
  ولا سبيل إلى استرجاعها."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    هل هذا القرص معمَّى، وكيف
    sudo syn-crypt change-key           استبدال عبارة المرور
    sudo syn-crypt add-key              إضافة عبارة ثانية
    sudo syn-crypt backup-header ملف    حفظ ترويسة LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  يعني أن البيانات لا تُسترجَع حتى مع عبارة المرور الصحيحة."
  ["Remove installation media and press ENTER to reboot..."]="أزل وسيط التثبيت واضغط ENTER لإعادة التشغيل..."
  ["Install SynapseOS     — right here, in this terminal"]="تثبيت SynapseOS      — هنا، في هذه الطرفية"
  ["Install graphically   — starts the desktop first"]="تثبيت رسومي          — يبدأ سطح المكتب أولًا"
  ["Try the live desktop  — look around; install later"]="تجربة سطح المكتب الحيّ — تصفَّح؛ وثبّت لاحقًا"
  ["Target:"]="الهدف:"
  ["ALONGSIDE"]="جنبًا إلى جنب"
  ["ERASE"]="امحُ"
  ["ADVANCED"]="متقدّم"
  ["Encrypt this installation?"]="أعمّي هذا التثبيت؟"
  ["There is no recovery."]="لا سبيل إلى الاسترجاع."
  ["Root filesystem"]="نظام الملفات الجذر"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — الافتراضي. ممل ومجرَّب ويُصلَح بأي أداة."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — لقطات وضغط zstd. تراجَع عن تحديث سيّئ من قائمة الإقلاع.
                    يستهلك ذاكرة ومعالجًا أكثر."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — سريع مع الملفات الكبيرة. لا لقطات، ولا يمكن تصغيره بعد
                    إنشائه."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — صُنع للذاكرة الوميضية. جيد على بطاقات SD والأقراص الرخيصة؛
                    نادر بما يكفي ليعرفه عدد قليل من أدوات الإنقاذ."
  ["Bootloader"]="محمِّل الإقلاع"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — الافتراضي. يكتشف أنظمة التشغيل الأخرى، وهو الوحيد
                          هنا القادر على الإقلاع من لقطة btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — أدنى ما يمكن. لا اكتشاف للأنظمة ولا قائمة لقطات."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — حديث وسريع، وهو يستطيع الإقلاع من اللقطات."
  ["Automatic snapshots?"]="لقطات تلقائية؟"
  ["Review the plan — nothing has been written yet:"]="راجع الخطة — لم يُكتب شيء بعد:"
  ["nothing else is touched"]="ولا يُمَس شيء آخر"
  ["not"]="لن"
  ["Partition"]="قسِّم"
  ["now."]="الآن."
  ["Partitions now on"]="الأقسام الآن على"
  ["These partitions will be FORMATTED"]="ستُهيَّأ هذه الأقسام"
  ["Full      — Standard + Steam + Nix + more software"]="كامل     — القياسي + Steam + Nix + مزيد من البرامج"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="قياسي    — طقم SynapseOS، وFirefox، ونموذج الذكاء الاصطناعي،"
  ["Minimal   — core daemons only: none of the above"]="أدنى     — الخُدُم الأساسية فقط: لا شيء مما سبق"
  ["Custom    — tick every package yourself, ours and"]="مخصّص    — أشِّر على كل حزمة بنفسك، حزمنا و"
  ["Which AI model should this machine run?"]="أي نموذج ذكاء اصطناعي يشغّله هذا الجهاز؟"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4.1 غ.ب   موصى به — وعليه ضُبِط SynapseOS"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2.2 غ.ب   نصف الحجم، وأضعف بوضوح"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0.4 غ.ب   يتّسع في أي مكان، ويجيب بقدره"
  ["None                            skip it — nothing else changes"]="بلا نموذج                        تخطَّه — ولا يتغير شيء آخر"
  ["Installing:"]="سيُثبَّت:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — مؤلِّف Wayland مبني للذكاء الاصطناعي  (الافتراضي)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — غير متاح: لم يُختَر synui"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — سطح مكتب Wayland كامل المزايا"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — سطح مكتب Wayland نظيف وحديث"
  ["TTY only   — No GUI (headless/server)"]="نصي فقط    — بلا واجهة رسومية (بلا شاشة/خادوم)"
  ["Disk:"]="القرص:"
  ["Boot:"]="الإقلاع:"
  ["Encrypted:"]="معمَّى:"
  ["Desktop:"]="سطح المكتب:"
  ["User:"]="المستخدم:"
  ["Hostname:"]="اسم المضيف:"
  ["Back up the header to another machine."]="احتفظ بنسخة من الترويسة على جهاز آخر."
  ["%s is mounted — unmount it first\\n"]="‏%s موصول — افصله أولًا
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="‏%s حجمه %s م.ب — و%s يحتاج %s م.ب على الأقل
"
  ["  Generating %s (a few seconds)...\\n"]="  يجري توليد %s (بضع ثوانٍ)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="اللغة: %s  (%s، لوحة المفاتيح %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  يحوي هذا القرص أصلًا %s قسمًا، وقسم نظام EFI (%s)،
  و%s غيغابايت من المساحة الحرة.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) ثبّت %s — استعمل المساحة الحرة واحتفظ بكل ما عداها
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s القرص كله — احذف كل قسم وكل البيانات
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — قسّم هذا القرص بنفسك، ثم اختر الأقسام
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s القرص كله — احذف كل قسم وكل البيانات  (الافتراضي)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — قسّم هذا القرص بنفسك، ثم اختر الأقسام
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s إن نسيت عبارة المرور ضاعت البيانات —
  لا إعادة تعيين، ولا اتصال بالدعم، ولا شيء.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  يأخذ snapper لقطة قبل كل عملية pacman وبعدها، وتظهر في %s قائمة
  للإقلاع من أي منها. فيصير التحديث السيّئ إعادة تشغيل
  بدل قرص إنقاذ.
"
  ["    Disk          : %s\\n"]="    القرص         : %s
"
  ["    Firmware      : %s\\n"]="    البرنامج الثابت: %s
"
  ["    Filesystem    : %s\\n"]="    نظام الملفات  : %s
"
  ["    Bootloader    : %s\\n"]="    محمِّل الإقلاع : %s
"
  ["    Separate /boot: %s\\n"]="    ‏/boot منفصل: %s
"
  ["    Encryption    : %s\\n"]="    التعمية       : %s
"
  ["    Snapshots     : %s\\n"]="    اللقطات       : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  يجري تعمية %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  يجري تهيئة قسم الجذر (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • أبقِ    كل الأقسام الموجودة وعددها %s، ومنها Windows
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • أعد استعمال %s كقسم EFI (موصول، %s مهيَّأ)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • أنشئ    جذر ext4 جديدًا بنحو %s غيغابايت في المساحة الحرة
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  يجري إنشاء قسم الجذر في المساحة الحرة (%s م.ب–%s م.ب)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  يجري تهيئة الجذر الجديد (%s، ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s سيعيد المثبِّت قراءة الجدول عند خروجك.
"
  ["    • a root partition, at least %s GiB\\n"]="    • قسم جذر، بحجم %s غيغابايت على الأقل
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • قسم ‏/boot منفصل بنحو 1 غيغابايت — %s بهذا الترتيب لا يقرأ الجذر
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  يجري تشغيل %s على %s — اكتب تغييراتك قبل الخروج.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    ‏%s هو أصلًا قسم تبديل — قد يستأنف منه نظام آخر.
"
  ["  Everything else on %s is left untouched.\\n"]="  ويبقى كل ما عدا ذلك على %s كما هو.
"
  ["  Making swap on %s...\\n"]="  يجري إنشاء التبديل على %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  يجري تهيئة قسم EFI (%s م.ب)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  اكتُشف معالج NVIDIA رسومي — يجري تثبيت %s (يبني الوحدة، وسيستغرق وقتًا)...
"
  ["  Installing video stack: %s %s...\\n"]="  يجري تثبيت طبقة العرض: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  فُعِّل [cachyos] (%s حزمة متاحة)
"
  ["  Language: %s  (chosen at boot)\\n"]="  اللغة: %s  (اختيرت عند الإقلاع)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  اللغة: %s   لوحة المفاتيح: %s (الطرفية) / %s (سطح المكتب)
"
  ["  Installing fonts (%s)...\\n"]="  يجري تثبيت الخطوط (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  يجري تنزيل نموذج الذكاء الاصطناعي (%s) — وهذا هو الجزء الطويل
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  لم يُبنَ شيء بعد. بصفتك %s، بعد أول إقلاع:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  يجري إضافة خطّاف %s إلى mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  يجري تثبيت GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="نعم — LUKS2 على %s"
  ["  Admin: use %s with your user password.\\n"]="  الإدارة: استعمل %s مع كلمة سر مستخدمك.
"
  ["  Manage it later with %s:\\n"]="  أدِرها لاحقًا بـ %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s ترويسة LUKS التالفة
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="‏%s على جهاز الإقلاع الحيّ — وهو وسيط المثبِّت نفسه
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    ‏%s هو أصلًا FAT — وقد يحمل محمِّل إقلاع نظام آخر.
"
  ["  Creating user '%s'...\\n"]="  يجري إنشاء المستخدم '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  أُنشئ المستخدم '%s' (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  لِج بعد إعادة التشغيل باسم '%s'.
"
  ["  Type '%s' to get started.\\n"]="  اكتب '%s' للبدء.
"
  ["Install SynapseOS"]="تثبيت SynapseOS"
  ["SynapseOS packages"]="حزم SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="كل ما يتكوّن منه النظام. ما لا يمكن إلغاؤه هو ما يعتمد عليه شيء آخر أبقيته — تُعاد هذه العناصر إلى التشغيل وتُذكر بالاسم قبل تثبيت أي شيء."
  ["SYNAPSE UI — the Wayland desktop"]="‏SYNAPSE UI — سطح مكتب Wayland"
  ["synapd — the local AI daemon"]="‏synapd — خدمة الذكاء الاصطناعي المحلية"
  ["synsh — the AI-native shell"]="‏synsh — الصدفة المهيّأة للذكاء الاصطناعي"
  ["synguard + kernel module"]="‏synguard + وحدة النواة"
  ["synnet — network policy"]="‏synnet — سياسة الشبكة"
  ["Software — the package manager"]="‏Software — مدير الحزم"
  ["Files — the file manager"]="الملفات — مدير الملفات"
  ["Terminal (synui depends on it)"]="الطرفية (‏synui يحتاج إليها)"
  ["Settings"]="الإعدادات"
  ["Disks"]="الأقراص"
  ["Editor"]="المحرّر"
  ["Calendar"]="التقويم"
  ["File Vault — a locked folder"]="خزنة الملفات — مجلد مقفل"
  ["Disk Cleanup — caches, and secure delete"]="تنظيف القرص — الذواكر المؤقتة والحذف الآمن"
  ["syn-update — how fixes arrive"]="‏syn-update — من هنا تصل الإصلاحات"
  ["syn — the top-level CLI"]="‏syn — سطر الأوامر الرئيسي"
  ["syn-model — fetch AI models"]="‏syn-model — جلب نماذج الذكاء الاصطناعي"
  ["syn-confine — the sandbox"]="‏syn-confine — صندوق العزل"
  ["fetch — the About OS readout"]="‏fetch — ملخص النظام"
  ["Arcade — overlay, pads, big screen"]="‏Arcade — طبقة العرض ويد التحكم والشاشة الكبيرة"
  ["cliamp — the music player"]="‏cliamp — مشغّل الموسيقى"
  ["Player — playlists, shuffle and history, on mpv"]="‏Player — قوائم التشغيل والعشوائي والسجل، فوق mpv"
  ["Studio — photo darkroom and video"]="‏Studio — غرفة تحميض الصور والفيديو"
  ["GeForce NOW — cloud gaming in a browser"]="‏GeForce NOW — ألعاب سحابية داخل المتصفح"
  ["Arsenal — BlackArch browser"]="‏Arsenal — تصفّح BlackArch"
  ["Chibi — voice companion"]="‏Chibi — رفيق صوتي"
  ["Vibe — AI coding assistant"]="‏Vibe — مساعد برمجة بالذكاء الاصطناعي"
  ["Animated wallpapers (~317 MB)"]="خلفيات متحركة (~317 م.ب)"
  ["Nexus Chat (pulls in Firefox)"]="‏Nexus Chat (يجلب معه Firefox)"
  ["TEPRIS (pulls in Firefox)"]="‏TEPRIS (يجلب معه Firefox)"
  ["Web and communication"]="الوِب والتواصل"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="لا شيء من هذا لنا؛ كل اسم موجود في مستودعات Arch. ‏Firefox مفعّل افتراضياً لأن SynapseOS المثبَّت كان يصل من قبل بلا أي متصفح."
  ["Thunderbird — mail"]="‏Thunderbird — البريد"
  ["KeePassXC — passwords"]="‏KeePassXC — كلمات السر"
  ["Syncthing — file sync"]="‏Syncthing — مزامنة الملفات"
  ["LocalSend — send to phone (Flatpak)"]="‏LocalSend — الإرسال إلى الهاتف (Flatpak)"
  ["Audio and video"]="الصوت والفيديو"
  ["Office and graphics"]="المكتب والرسوميات"
  ["Development and admin"]="التطوير والإدارة"
  ["VS Code (OSS build)"]="‏VS Code (بناء OSS)"
  ["7zip + unrar"]="‏7zip + unrar"
  ["Games, launchers and helpers"]="الألعاب والمشغّلات والأدوات المساعدة"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="‏Steam موجود في الخيارات أدناه لا هنا: فهو الوحيد الذي يفعّل معمارية ثانية ومستودعاً ثالثاً."
  ["Prism — Minecraft"]="‏Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="‏Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="‏PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="‏Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="‏GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="‏AntiMicroX — إعادة تعيين أزرار يد التحكم"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="لا يوجد اتصال. ينزّل SynapseOS النظام الأساسي أثناء التثبيت، لذا يلزم وجود شبكة عاملة قبل أن يبدأ."
  ["Choose a disk to install to."]="اختر قرصاً للتثبيت عليه."
  ["The encryption passphrase needs at least 8 characters."]="تحتاج عبارة التعمية إلى 8 محارف على الأقل."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="بلا مدير حزم وبلا سطح مكتب، لن يكون لهذا التثبيت أي طريقة لإعادة أي منهما. أبقِ واحداً على الأقل."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="اسم المستخدم يتكوّن من حروف صغيرة وأرقام و - و _ ، ولا يمكن أن يبدأ برقم."
  ["Set a password for the account."]="عيّن كلمة سر للحساب."
  ["The two passwords do not match."]="كلمتا السر غير متطابقتين."
  ["A locale is needed, e.g. en_US.UTF-8."]="يلزم تحديد محليّة، مثل ar_SA.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="تلزم منطقة زمنية، مثل Asia/Riyadh."
  ["printing"]="الطباعة"
  ["%1 repo"]="مستودع %1"
  ["Disk"]="القرص"
  ["Mode"]="الوضع"
  ["Filesystem"]="نظام الملفات"
  ["%1 on LUKS2"]="‏%1 فوق LUKS2"
  ["%1 + snapshots"]="‏%1 + لقطات"
  ["Install"]="التثبيت"
  ["none"]="لا شيء"
  ["Account"]="الحساب"
  ["Desktop"]="سطح المكتب"
  ["Locale"]="المحليّة"
  ["%1   keys %2 / %3"]="‏%1   مفاتيح %2 / %3"
  ["Timezone"]="المنطقة الزمنية"
  ["%1 package(s) — WITHOUT %2"]="‏%1 حزمة — دون %2"
  ["%1 package(s)"]="‏%1 حزمة"
  ["Software"]="Software"
  ["Options"]="الخيارات"
  ["Could not write the install profile."]="تعذّرت كتابة ملف إعداد التثبيت."
  ["Installation complete."]="اكتمل التثبيت."
  ["Installation failed — see the log."]="فشل التثبيت — راجع السجل."
  ["No network connection"]="لا يوجد اتصال بالشبكة"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="ينزّل النظام الأساسي أثناء التثبيت، لذا لا يمكن البدء دون اتصال. صِل كبلاً أو انضم إلى شبكة ثم اضغط «أعد الفحص» — تبقى إجاباتك في هذه الصفحات محفوظة."
  ["Wi-Fi settings"]="إعدادات الواي-فاي"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="هنا يُسأل عن قرص وحساب وبعض التفضيلات، ثم تُسلَّم الإجابات إلى المثبِّت نفسه الذي تشغّله النسخة النصية. لا يُكتب شيء على أي قرص حتى الصفحة الأخيرة، وتلك الصفحة تقول بالضبط ما هي مقبلة عليه."
  ["A disk is partitioned and formatted"]="يُقسَّم قرص ويُهيّأ"
  ["The base system and the SynapseOS packages are installed"]="يُثبَّت النظام الأساسي وحزم SynapseOS"
  ["An account and a desktop are set up"]="يُنشأ حساب ويُهيّأ سطح مكتب"
  ["A bootloader is written"]="يُكتب محمّل إقلاع"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="تقسيم مخطط قائم يدوياً هو وضع ADVANCED في المثبِّت النصي — أغلق هذه النافذة ونفّذ \`syn-install\` في طرفية لذلك."
  ["Where should SynapseOS go?"]="أين يذهب SynapseOS؟"
  ["The installer's own media is listed and cannot be chosen."]="وسيط المثبِّت نفسه مذكور في القائمة ولا يمكن اختياره."
  ["No disks found."]="لم يُعثر على أقراص."
  ["Erase the disk"]="محو القرص"
  ["every partition and all data"]="كل قسم وكل البيانات"
  ["Install alongside"]="التثبيت جنباً إلى جنب"
  ["use free space, UEFI only"]="استخدام المساحة الحرة، UEFI فقط"
  ["Snapshots"]="اللقطات"
  ["btrfs + limine only"]="‏btrfs + limine فقط"
  ["Encrypt the disk"]="تعمية القرص"
  ["Passphrase"]="عبارة التعمية"
  ["8 characters or more"]="‏8 محارف أو أكثر"
  ["What should be installed?"]="ما الذي يُثبَّت؟"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="نواة SynapseOS — المؤلِّف والخدمات والتطبيقات التي بُني عليها — تُثبَّت مع أي اختيار هنا."
  ["Full"]="كامل"
  ["Standard + Steam + Nix + more software"]="قياسي + Steam + Nix + برمجيات إضافية"
  ["Standard"]="قياسي"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="طقم SynapseOS و Firefox ونموذج الذكاء الاصطناعي والبلوتوث والطباعة و Wine والهاتف"
  ["Minimal"]="أدنى"
  ["core daemons only — no apps, no software, no model"]="خدمات النواة فقط — بلا تطبيقات ولا برمجيات ولا نموذج"
  ["Custom"]="مخصّص"
  ["tick every package yourself, ours and the ordinary software"]="اختَر كل حزمة بنفسك، حزمنا والبرمجيات المعتادة"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="ليست حزماً: مستودع أو معمارية أو خدمة. كل واحد منها قرار له أثر لا يتّسع له مربع اختيار في الأعلى."
  ["Printing (CUPS)"]="الطباعة (CUPS)"
  ["Wine — run Windows .exe/.msi"]="‏Wine — تشغيل ملفات ‎.exe/.msi‎ الخاصة بويندوز"
  ["KDE Connect — pair a phone"]="‏KDE Connect — إقران هاتف"
  ["Steam + game stack + Proton (~3.1 GB)"]="‏Steam + طقم الألعاب + Proton (~3.1 غ.ب)"
  ["BlackArch repo — ~5000 tools, none installed"]="مستودع BlackArch — نحو 5000 أداة، لا يُثبَّت منها شيء"
  ["Nix + Home Manager"]="‏Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="‏syn-update معطّل: لن يكون لهذا الجهاز أي وسيلة لتلقّي حزمة SynapseOS أخرى. إصلاح ذلك لاحقاً يعني تثبيته يدوياً من صورة ISO، أو إعادة التثبيت."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="‏synui معطّل: لن يكون هذا سطح مكتب SynapseOS. تعرض صفحة سطح المكتب خيارات KDE أو GNOME أو بلا واجهة رسومية."
  ["AI model — downloaded during the install"]="نموذج ذكاء اصطناعي — يُنزَّل أثناء التثبيت"
  ["~4.1 GB — recommended"]="‏~4.1 غ.ب — موصى به"
  ["~2.2 GB — weaker"]="‏~2.2 غ.ب — أضعف"
  ["~0.4 GB — much weaker"]="‏~0.4 غ.ب — أضعف بكثير"
  ["None"]="لا شيء"
  ["AI stays inert"]="يبقى الذكاء الاصطناعي خاملاً"
  ["NVIDIA GPU inference"]="الاستدلال على معالج NVIDIA الرسومي"
  ["the CUDA runtime, ~4.7 GiB"]="زمن تشغيل CUDA، نحو 4.7 غيغابايت"
  ["Who is this machine for?"]="لمن هذا الجهاز؟"
  ["Username"]="اسم المستخدم"
  ["lower-case, no spaces"]="حروف صغيرة، بلا فراغات"
  ["Full name (optional)"]="الاسم الكامل (اختياري)"
  ["Password"]="كلمة السر"
  ["Password again"]="كلمة السر مرة أخرى"
  ["They do not match"]="غير متطابقتين"
  ["the native compositor"]="المؤلِّف الأصلي"
  ["synui is not selected"]="‏synui غير محدّد"
  ["headless"]="بلا واجهة رسومية"
  ["Language, keyboard and time"]="اللغة ولوحة المفاتيح والوقت"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="اختر لغة وتتبعها الثلاث الأخرى. خريطة مفاتيح الطرفية وتخطيط سطح المكتب منفصلان عمداً — فالسويدية هي 'sv-latin1' للطرفية و 'se' لسطح المكتب — كي يمكن تغيير كل منهما وحده لاحقاً."
  ["Language"]="اللغة"
  ["sets the keyboard and the fonts too"]="تضبط لوحة المفاتيح والخطوط أيضاً"
  ["typed by hand — fonts cover as much as possible"]="مكتوب يدوياً — تغطّي الخطوط أكبر قدر ممكن"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="تضبط المحليّة واسمَي لوحة المفاتيح وحزمة الخطوط. ويمكن بدلاً من ذلك كتابة أي محليّة تعرفها glibc."
  ["The common zones first, then every name tzdata ships."]="المناطق الشائعة أولاً، ثم كل اسم تأتي به tzdata."
  ["Console keymap"]="خريطة مفاتيح الطرفية"
  ["loadkeys — the text console and the greeter"]="‏loadkeys — الطرفية النصية وشاشة الولوج"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="كل خريطة مفاتيح تستطيع هذه الصورة تحميلها. هذه تسمّي ملفاً على loadkeys أن يجده، ولهذا ليست القائمة نفسها الخاصة بتخطيط سطح المكتب."
  ["Desktop layout"]="تخطيط سطح المكتب"
  ["XKB — the compositor"]="‏XKB — المؤلِّف"
  ["Desktop keyboard layout"]="تخطيط لوحة مفاتيح سطح المكتب"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="التخطيطات التي يستطيع xkbcommon ترجمتها. ‏'uk' خريطة مفاتيح طرفية وليست تخطيطاً هنا — التخطيط اسمه 'gb'."
  ["Read this back"]="راجع هذا مرة أخرى"
  ["Nothing has been written yet. The next button is the one that starts."]="لم يُكتب شيء بعد. الزر التالي هو الذي يبدأ."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="سيُحذف كل قسم على %1"
  ["SynapseOS will be installed into the free space on %1"]="سيُثبَّت SynapseOS في المساحة الحرة على %1"
  ["SynapseOS is installed"]="تم تثبيت SynapseOS"
  ["The install stopped"]="توقّف التثبيت"
  ["Installing SynapseOS"]="جارٍ تثبيت SynapseOS"
  ["Reboot and remove the installation media."]="أعد التشغيل وانزع وسيط التثبيت."
  ["The log below is the whole story — the last lines say why."]="السجل أدناه يحكي القصة كاملة — السطور الأخيرة تقول السبب."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="يستغرق هذا وقتاً: يُنزَّل النظام الأساسي والحزم، ونموذج الذكاء الاصطناعي وحده بحجم غيغابايتات."
  ["Back"]="رجوع"
  ["Next"]="التالي"
  ["Reboot"]="إعادة التشغيل"
  ["Close"]="إغلاق"
  ["type to filter, or type a name that is not listed"]="اكتب للتصفية، أو اكتب اسماً غير مذكور في القائمة"
  ["Nothing to list on this image — type the name instead."]="لا شيء لعرضه على هذه الصورة — اكتب الاسم بدلاً من ذلك."
  ["Nothing matches — the row below uses what you typed."]="لا تطابق — يستعمل السطر أدناه ما كتبته."
  ["Use “%1” as typed"]="استعمل «%1» كما كُتب"
)
