-- DESCRIPTION: Verilator: VHDL a qualified expression keeps its scalar type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    x   : in  integer;
    q1  : out integer;
    q2  : out integer;
    c1_o : out integer;
    c2_o : out integer
  );
end;

architecture rtl of t is
  -- A qualified expression has the type of its mark (IEEE 1076-2008 9.3.5):
  -- integer'(x) stays an integer while folding and at run time. GHDL:
  -- x = -5 gives q1 = -2 and q2 = 1, and the constants are -2 and 1.
  constant c1 : integer := integer'(-5) / 2;
  constant c2 : boolean := integer'(-1) < 0;
begin
  q1 <= integer'(x) / 2;
  q2 <= 1 when integer'(x) < 0 else 0;
  c1_o <= c1;
  c2_o <= 1 when c2 else 0;
end;
