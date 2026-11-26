-- DESCRIPTION: Verilator: VHDL use clause scoped to one design unit
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A context clause applies only to the design unit that follows it and that unit's
-- architectures (IEEE 1076-2008 12.4, 12.1), so a second entity in the file needs its own.
-- GHDL rejects each type below with "no declaration".

library ieee;
use ieee.std_logic_1164.all;

entity first is
  port (a : in std_logic; y : out std_logic);
end;

architecture rtl of first is
begin
  y <= a;
end;

entity second is
  port (a : in std_logic; y : out std_logic);
end;

architecture rtl of second is
begin
  y <= a;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (a : in std_logic; y : out std_logic);
end;

-- numeric_std is not named by any clause of this unit
architecture rtl of t is
  signal s : unsigned(3 downto 0);
begin
  y <= a;
end;
