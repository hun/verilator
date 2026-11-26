-- DESCRIPTION: Verilator: VHDL port actuals must conform to the formal
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The actual's type must conform to the port's (IEEE 1076-2008 6.5.7.1);
-- GHDL refuses both instantiations below.

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (
    p : in  std_logic_vector(7 downto 0);
    y : out std_logic
  );
end;

architecture rtl of child is
begin
  y <= p(0);
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a4 : in  std_logic_vector(3 downto 0);
    b4 : in  bit_vector(3 downto 0);
    y  : out std_logic
  );
end;

architecture rtl of t is
  signal s8 : std_logic_vector(7 downto 0);
begin
  -- A width mismatch: a four-bit actual on an eight-bit port
  u1 : entity work.child port map (p => a4, y => y);
  -- An element-kind mismatch: a bit_vector actual on a std_logic_vector port
  u2 : entity work.child port map (p => b4, y => open);
end;
