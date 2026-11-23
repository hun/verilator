-- DESCRIPTION: Verilator: VHDL succ, pred, leftof, rightof, pos and val of an integer type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    i  : in  integer;
    o1 : out integer;
    o2 : out integer;
    o3 : out integer;
    o4 : out integer;
    o5 : out integer;
    o6 : out integer;
    o7 : out integer;
    o8 : out integer;
    s  : out integer
  );
end;

architecture rtl of t is
  subtype down is natural range 10 downto 1;
  constant c1 : integer := integer'succ(4);
  constant c2 : integer := down'leftof(4);
  constant c3 : integer := down'rightof(4);
  constant c4 : integer := boolean'pos(true);
  constant c5 : integer := integer'pred(0);
  constant c6 : integer := natural'rightof(3);
begin
  o1 <= integer'succ(i);
  o2 <= integer'pred(i);
  o3 <= natural'leftof(i);
  o4 <= natural'rightof(i);
  o5 <= down'leftof(i);
  o6 <= down'rightof(i);
  o7 <= integer'pos(i);
  o8 <= integer'val(i);
  s  <= c1 * 100000 + c2 * 10000 + c3 * 1000 + c4 * 100 + (c5 + 10) * 10 + c6;
end;
