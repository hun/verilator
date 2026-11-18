-- DESCRIPTION: Verilator: unconstrained record ports fixed by element actuals
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An unconstrained record port takes each element's range from the association
-- that names it; two instances at different widths are separate specializations.
-- A whole record actual also fixes them, including a record whose remaining
-- elements are constrained by its declaration.

library ieee;
use ieee.std_logic_1164.all;

package recunpkg is
  type pair_u is record
    x : std_logic_vector;
    y : std_logic_vector;
  end record;
  type mixed_u is record
    fixed : std_logic_vector(3 downto 0);
    anyv  : std_logic_vector;
    flag  : std_logic;
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recunpkg.all;

entity child_u is
  port (
    i : in pair_u;
    o : out pair_u
  );
end;

architecture rtl of child_u is
begin
  o.x <= i.x;
  o.y <= i.y;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.recunpkg.all;

entity child_m is
  port (
    i : in mixed_u;
    o : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of child_m is
begin
  o <= i.fixed xor i.anyv when i.flag = '1' else i.fixed;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.recunpkg.all;

entity t is
  port (
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    c  : in  std_logic_vector(2 downto 0);
    d  : in  std_logic_vector(1 downto 0);
    w  : in  std_logic_vector(3 downto 0);
    f  : in  std_logic;
    ux : out std_logic_vector(3 downto 0);
    uy : out std_logic_vector(3 downto 0);
    vx : out std_logic_vector(2 downto 0);
    vy : out std_logic_vector(1 downto 0);
    mo : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  signal m : mixed_u(anyv(3 downto 0));
begin
  -- Element-only associations at two different widths
  u1 : entity work.child_u
    port map (
      i.x => a,
      i.y => b,
      o.x => ux,
      o.y => uy
    );

  u2 : entity work.child_u
    port map (
      i.x => c,
      i.y => d,
      o.x => vx,
      o.y => vy
    );

  -- A whole record actual, with the unconstrained element constrained by m's
  -- subtype
  m.fixed <= w;
  m.anyv <= a;
  m.flag <= f;

  v : entity work.child_m
    port map (
      i => m,
      o => mo
    );
end;
