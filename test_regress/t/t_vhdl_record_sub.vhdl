-- DESCRIPTION: Verilator: VHDL constrained record subtypes
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package recpkg is
  type rec_t is record
    x : std_logic_vector;
    y : std_logic_vector;
  end record;

  subtype rec_a is rec_t(x(3 downto 0));
  subtype rec_b is rec_a(y(7 downto 0));
  subtype rec_c is rec_t(x(7 downto 0), y(3 downto 0));
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpkg.all;

-- The port type is the unconstrained record; its member widths come from the actual
entity child is
  port (
    i : in rec_t;
    o : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of child is
begin
  o <= i.y;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.recpkg.all;

entity t is
  port (
    a4 : in std_logic_vector(3 downto 0);
    b8 : in std_logic_vector(7 downto 0);
    oax : out std_logic_vector(3 downto 0);
    oay : out std_logic_vector(7 downto 0);
    obx : out std_logic_vector(3 downto 0);
    oby : out std_logic_vector(7 downto 0);
    ocx : out std_logic_vector(7 downto 0);
    ocy : out std_logic_vector(3 downto 0);
    co : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  signal r1 : rec_b;
  signal r2 : rec_b;
  signal rc : rec_c;
begin
  -- Aggregate of a constrained subtype
  r1 <= (x => a4, y => b8);
  -- Member assignment of a subtype that completes a parent constraint
  r2.x <= a4;
  r2.y <= b8;
  -- A different constraint set on the same base record type
  rc <= (x => b8, y => a4);

  oax <= r1.x;
  oay <= r1.y;
  obx <= r2.x;
  oby <= r2.y;
  ocx <= rc.x;
  ocy <= rc.y;

  u : entity work.child port map (i => r1, o => co);
end;
