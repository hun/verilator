-- DESCRIPTION: Verilator: VHDL enumeration range subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    y : out integer;
    z : out integer;
    v : out integer;
    w : out integer
  );
end;

architecture rtl of t is
  type st is (S0, S1, S2, S3, S4);
  subtype mid is st range S1 to S3;
  subtype top is mid range S2 to S3;
  signal s : mid := S2;
  constant k : st := S3;
begin
  y <= st'pos(mid'left) * 100 + st'pos(mid'right) * 10 + st'pos(top'low);
  z <= st'pos(mid'succ(S1)) + st'pos(top'high) * 10 + mid'pos(mid'val(2)) * 100;
  v <= st'pos(s) + st'pos(k);
  w <= st'pos(top'pred(S3)) * 10 + st'pos(mid'pred(S2));
end;
