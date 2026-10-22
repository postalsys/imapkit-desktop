# Where the Windows build machine is, sourced by the win-*.sh scripts. Override with WIN_HOST and WIN_DIR
WIN_HOST=${WIN_HOST:-andri@10.8.0.8}
# the source and build directory on that machine, in Windows form
WIN_DIR=${WIN_DIR:-'C:\Users\andri\src\imapkit-desktop'}
# the same with forward slashes, for scp and tar
WIN_DIR_FWD=${WIN_DIR//\\//}
