-- DESCRIPTION: Verilator: VHDL component declaration and component instantiation
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package comp_pkg is
  -- A component declaration in a package: the architecture that instantiates it names only
  -- the component, so the entity is bound by name
  component adder is
    generic (
      width : positive := 4
    );
    port (
      a : in  std_logic_vector(width - 1 downto 0);
      b : in  std_logic_vector(width - 1 downto 0);
      y : out std_logic_vector(width - 1 downto 0)
    );
  end component adder;
end package comp_pkg;

library ieee;
use ieee.std_logic_1164.all;

entity adder is
  generic (
    width : positive := 4
  );
  port (
    a : in  std_logic_vector(width - 1 downto 0);
    b : in  std_logic_vector(width - 1 downto 0);
    y : out std_logic_vector(width - 1 downto 0)
  );
end entity adder;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture rtl of adder is
begin
  y <= std_logic_vector(unsigned(a) + unsigned(b));
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.comp_pkg.all;

entity t is
  port (
    o_comp : out std_logic_vector(3 downto 0);  -- Named component, generic map
    o_kw   : out std_logic_vector(3 downto 0);  -- `component NAME` spelling, no generic map
    o_ent  : out std_logic_vector(3 downto 0)   -- Entity instantiation, the control
  );
end entity t;

architecture rtl of t is
  signal a, b : std_logic_vector(3 downto 0);
  signal y_comp, y_kw, y_ent : std_logic_vector(3 downto 0);
begin
  a <= "0011";
  b <= "0101";

  -- Declared in comp_pkg, so no component declaration is visible in this architecture
  comp_inst : adder
    generic map (
      width => 4
    )
    port map (
      a => a,
      b => b,
      y => y_comp
    );

  -- The same entity through the `component` keyword, with the generic defaulted
  kw_inst : component adder
    port map (
      a => a,
      b => b,
      y => y_kw
    );

  ent_inst : entity work.adder
    port map (
      a => a,
      b => b,
      y => y_ent
    );

  o_comp <= y_comp;
  o_kw   <= y_kw;
  o_ent  <= y_ent;
end architecture rtl;
