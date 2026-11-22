-- DESCRIPTION: Verilator: VHDL hierarchy
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity and2 is
  port (
    a : in std_logic;
    b : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of and2 is
begin
  y <= a and b;
end;

library ieee;
use ieee.std_logic_1164.all;

entity or2 is
  port (
    a : in std_logic;
    b : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of or2 is
begin
  y <= a or b;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a  : in std_logic;
    b  : in std_logic;
    y  : out std_logic;
    y2 : out std_logic
  );
end;

architecture rtl of t is
  component and2 is
    port (
      a : in std_logic;
      b : in std_logic;
      y : out std_logic
    );
  end component;
begin
  -- Component binding: the UG901-compatible form for foreign modules
  u1 : and2 port map (a => a, b => b, y => y);
  -- Direct entity instantiation of a VHDL entity
  u2 : entity work.or2 port map (a => a, b => b, y => y2);
end;
