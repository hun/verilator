-- DESCRIPTION: Verilator: VHDL unbound input ports must have a default
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 6.5.6.3: an unassociated input takes its default; without a
-- default it is an error, whether it is omitted or explicitly open.

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (
    a : in  std_logic;
    b : out integer
  );
end;

architecture rtl of leaf is
begin
  b <= 1;
end;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
begin
  -- The named input is omitted
  u1 : entity work.leaf port map (b => o);
  -- The named input is explicitly open
  u2 : entity work.leaf port map (a => open, b => o);
end;
