#!/bin/bash

set -o errexit

EDFISTAT=../edfistat.py

#
# Example on how to use EDFISTAT to compare input-output faultload distributions
#

rm -rf tmp
mkdir tmp

echo " - Aggregate output distributions of a single run over N processes..."
for r in `echo run1 run2`
do
    $EDFISTAT edfi-div output_${r}_proc1.ini@edfi-faults:output_${r}_proc2.ini@edfi-faults sum > tmp/output_${r}_faults.ini
    $EDFISTAT edfi-div output_${r}_proc1.ini@edfi-candidates:output_${r}_proc2.ini@edfi-candidates sum > tmp/output_${r}_candidates.ini
    $EDFISTAT edfi-prob tmp/output_${r}_faults.ini@edfi-div:tmp/output_${r}_candidates.ini@edfi-div div > tmp/output_${r}.ini
#
#  OR (less accurate):
#
#  $EDFISTAT edfi-prob tmp/output_${r}_proc1.ini@edfi-prob:tmp/output_${r}_proc2.ini@edfi-prob average > tmp/output_${r}.ini
#
done

echo "   Done."

echo " - Compute the median output distribution over N runs..."
$EDFISTAT edfi-prob tmp/output_run1.ini@edfi-prob:tmp/output_run2.ini@edfi-prob median > tmp/output.ini
echo "   Done."

echo " - Compare output distribution with the input distribution and compute relative error for each fault type..."
$EDFISTAT edfi-prob input.ini@edfi-prob:tmp/output.ini@edfi-prob relerr > tmp/relerr.ini
echo "   Done."

echo " - Compute the median relative error (fault degradation)..."
$EDFISTAT edfi-prob tmp/relerr.ini@edfi-prob median > tmp/mre.ini
echo "   Done."

DIFF=`diff tmp/mre.ini mre.expected.ini 2>&1 || true`
if  [ "$DIFF" != "" ]; then
	echo " **** Test completed with the following deviation from expected behavior:"
	diff tmp/mre.ini mre.expected.ini 2>&1 || true
else
	echo " **** Test successful!"
fi

rm -rf tmp
