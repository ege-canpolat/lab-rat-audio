# Who makes these plugins. Every plugin's CMakeLists.txt and tools/make_pkg.sh read this file,
# so renaming the studio is a change here (plus the website text).
#
# DEVELOPER_NAME is what DAWs show ("<name> > Elsewhere"). DEVELOPER_ID goes into bundle IDs
# (lowercase, no spaces). DEVELOPER_CODE is the 4-character manufacturer code, with at least one
# capital letter. Changing the ID or code after release makes DAWs treat the plugins as new ones,
# so pick them before publishing.
set(DEVELOPER_NAME "Lab Rat Audio")
set(DEVELOPER_ID   "labrataudio")
set(DEVELOPER_CODE "LbRt")
