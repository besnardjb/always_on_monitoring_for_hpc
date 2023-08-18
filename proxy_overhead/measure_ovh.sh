#!/bin/sh

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
   echo "Running $@ ..." >&2
   RUN=$("$@" 2>/dev/null)

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

FREQS="0.00001 0.0001 0.001 0.01 0.1 1"


tau_metric_proxy -i -p 19999&

sleep 2

for f in $FREQS
do
   abort_if_file "proxy-$f.dat"
done

for FREQ in $FREQS
do

   CNT=0

   while test $CNT -lt 10;
   do

      echo "Measure $CNT.."

      TEST=$(run mpirun -np 4 tau_metric_proxy_run -m -F "$FREQ" ./test)
      echo "$TEST" >> "proxy-$FREQ.dat"

      CNT=$((CNT + 1))

   done

done

killall tau_metric_proxy

# Now run at frequency 0
CNT=0

while test $CNT -lt 10;
do

   echo "Measure $CNT.."

   TEST=$(run tau_metric_proxy_run -m ./test)
   echo "$TEST" >> "proxy-0.dat"

   CNT=$((CNT + 1))

done

python3 ./plot_ovh.py
