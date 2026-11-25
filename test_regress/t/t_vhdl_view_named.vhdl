-- DESCRIPTION: Verilator: VHDL interface view of an AXI-Stream record, all port forms
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package axis_pkg is
  type axis_rec_type is record
    ready : std_logic;
    valid : std_logic;
    data  : std_logic_vector(7 downto 0);
  end record axis_rec_type;

  view axis_master_view of axis_rec_type is
    ready : in;
    valid : out;
    data  : out;
  end view axis_master_view;

  alias axis_slave_view is axis_master_view'converse;
end package axis_pkg;

library ieee;
use ieee.std_logic_1164.all;
use work.axis_pkg.all;

entity pipe is
  port (
    clk_i  : in std_logic;
    rst_i  : in std_logic;
    s_axis : view axis_slave_view;
    m_axis : view axis_master_view of axis_rec_type
  );
end entity pipe;

architecture rtl of pipe is
begin
  s_axis.ready <= m_axis.ready or not m_axis.valid;

  process (clk_i)
  begin
    if rising_edge(clk_i) then
      if s_axis.ready = '1' then
        m_axis.data  <= s_axis.data;
        m_axis.valid <= s_axis.valid;
      end if;
      if rst_i = '1' then
        m_axis.valid <= '0';
      end if;
    end if;
  end process;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use work.axis_pkg.all;

entity t is
  port (
    clk_i      : in  std_logic;
    rst_i      : in  std_logic;
    sin_valid  : in  std_logic;
    sin_data   : in  std_logic_vector(7 downto 0);
    sin_ready  : out std_logic;
    mout_valid : out std_logic;
    mout_data  : out std_logic_vector(7 downto 0);
    mout_ready : in  std_logic
  );
end entity t;

architecture rtl of t is
  signal a, b, c : axis_rec_type;
begin
  a.valid <= sin_valid;
  a.data  <= sin_data;
  sin_ready <= a.ready;
  c.ready <= mout_ready;
  mout_valid <= c.valid;
  mout_data  <= c.data;

  -- Whole-record associations, positional and named
  u1 : entity work.pipe port map (clk_i, rst_i, a, b);

  -- Element associations
  u2 : entity work.pipe
    port map (
      clk_i => clk_i,
      rst_i => rst_i,
      s_axis.ready => b.ready,
      s_axis.valid => b.valid,
      s_axis.data  => b.data,
      m_axis       => c
    );
end architecture rtl;
