-- DESCRIPTION: Verilator: VHDL use clauses and qualified names that make standard types visible
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each unit below names its standard types a different way, and GHDL accepts all of them.

library ieee;

-- A selected name needs no use clause
entity by_selected is
  port (
    a : in  ieee.std_logic_1164.std_logic;
    y : out ieee.std_logic_1164.std_logic
  );
end;

architecture rtl of by_selected is
begin
  y <= a;
end;

library ieee;
use ieee.std_logic_1164.std_logic;
use ieee.numeric_std.unsigned;

-- A use clause naming one declaration makes only that declaration visible
entity by_declaration is
  port (
    a : in  std_logic;
    y : out unsigned(3 downto 0)
  );
end;

architecture rtl of by_declaration is
begin
  y <= (others => a);
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic;
    y : out std_logic
  );
end;

-- A use clause in a declarative part reaches only that architecture
architecture rtl of t is
  use ieee.numeric_std.all;
  signal s : unsigned(3 downto 0);
begin
  s <= (others => a);
  y <= s(0);
end;
