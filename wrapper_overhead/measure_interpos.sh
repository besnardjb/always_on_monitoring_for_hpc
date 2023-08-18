#!/bin/sh

make

#
# Get the number of ticks per second
#
TPS=$("./calibrate" 2>/dev/null)

if test $? != 0; then
   echo "Failed to call ./calibrate"
   return 1
fi

export "FORCED_TICKS=$TPS"
echo "Ticks are set to $FORCED_TICKS"

#
# Now run the experiments
#
run()
{
   echo "Running $1 ..." >&2
   RUN=$("$1" 2>/dev/null)

   if test $? != 0; then
      echo "Failed to call $1" >&2
      exit 1
   fi

   echo "$RUN"
}

abort_if_file()
{
   if test -f "$1"; then
      echo "Cannot run as $1 is already generated" >&2
      exit 1
   fi
}

for f in "./static.dat" "./tu.dat" "./lib.dat"
do
   abort_if_file "$f"
done

CNT=0

while test $CNT -lt 100;
do

   echo "Measure $CNT.."

   STATIC=$(run ./interpos_static)
   TU=$(run ./interpos_tu)
   LIB=$(run ./interpos_lib)

   CNT=$((CNT + 1))

   echo "$STATIC" >> "./static.dat"
   echo "$TU" >> "./tu.dat"
   echo "$LIB" >> "./lib.dat"

done

python3 ./plot_interpos.py
