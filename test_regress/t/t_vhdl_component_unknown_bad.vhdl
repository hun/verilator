-- DESCRIPTION: Verilator: VHDL component association names a declared port
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in  std_logic;
    o : out integer
  );
end;

architecture rtl of t is
  component leaf is
    port (
      a : in  std_logic;
      b : out integer
    );
  end component;
begin
  u : leaf port map (a => i, c => o);
end;
